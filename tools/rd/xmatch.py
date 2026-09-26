# SPDX-License-Identifier: GPL-3.0-only
"""Cross-runtime function matcher for the CommonLibF4RD port: given a Fallout 4
OG 1.10.163 function (or call site), find the SAME function (or call site) in
AE 1.11.240 by call-graph anchoring, not byte patterns (AE was recompiled with
a newer compiler, so `sigfind.py`'s byte-pattern approach loses far more often
here than it does OG<->OG or on smaller diffs).

The idea:
  1. CommonLibF4RD's headers already carry ~22.6k known OG<->AE function pairs,
     spelled `REL::ID(og_id, ae_id)`. Resolved through the F4SE Address Library
     .bin files these become (og_offset, ae_offset) ANCHORS: ground truth.
  2. Build a direct call graph (E8 rel32 calls, E9 rel32 tail-jumps) for each
     exe, with function boundaries from .pdata.
  3. To find the AE counterpart of an unanchored OG function F: look at F's OG
     CALLERS. Any caller that is itself an anchor has a known AE counterpart
     function; align the caller's OG call sequence against its AE call
     sequence (using OTHER anchored calls inside that same caller as alignment
     landmarks - a monotonic subsequence / patience-diff backbone), and read
     off whichever AE call lands at F's aligned position. Every anchored
     caller casts one such vote. F's own anchored CALLEES (if any) are used
     only to *confirm* a candidate (the candidate should call their AE
     counterparts too) -- not to vote, since that would be closer to circular.

Function boundaries: x64 SEH chains a function's continuation ranges (cold
paths, extra prologues) as separate .pdata RUNTIME_FUNCTION entries whose
UNWIND_INFO has UNW_FLAG_CHAININFO (bit 2 of the flags nibble, i.e.
`(first_byte >> 3) & 0x4`) set. Those do not start a new function - only a
non-chained RUNTIME_FUNCTION entry does. So the function-start set is the
non-chained BeginAddress values, and a function's range is
[start, next_non_chained_start).

CLI:
    python xmatch.py fn <og_hex>          find an AE function for an OG one
    python xmatch.py site <og_hex>        find an AE call site for an OG one
    python xmatch.py selftest [N]         hide N real anchors, try to recover them
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import pickle
import random
import re
import sys
from pathlib import Path

import capstone
import pefile

import addrlib

ROOT = Path(__file__).resolve().parent
CACHE_DIR = ROOT / ".cache"
CACHE_DIR.mkdir(parents=True, exist_ok=True)

OG_EXE = r"D:\GOGGames\Fallout 4 GOTY\Fallout4.exe"
AE_EXE = r"D:\SteamLibrary\steamapps\common\Fallout 4 AE\Fallout4.exe"
OG_BIN = r"D:\GOGGames\Fallout 4 GOTY\Data\F4SE\Plugins\version-1-10-163-0.bin"
# NOTE: D:\SteamLibrary\...\Fallout 4 AE has no Data\F4SE\Plugins at all (no F4SE
# install there). D:\SteamFreeGames\Fallout 4 AE is a byte-identical copy of the same
# 1.11.240 Fallout4.exe (md5 998299c7...) and DOES carry the matching address-library
# bin (md5 a131d495... - also identical to the version-1-11-240-0.bin GOG bundles
# alongside the OG one). Using it is not a version substitution, it's the same table.
AE_BIN = r"D:\SteamFreeGames\Fallout 4 AE\Data\F4SE\Plugins\version-1-11-240-0.bin"

CLIB_ROOTS = [
    Path(r"C:\Users\DuduPhudu\Documents\Projects\CommonLibF4RD\CommonLibF4\include"),
    Path(r"C:\Users\DuduPhudu\Documents\Projects\CommonLibF4RD\CommonLibF4\src"),
]
_SRC_EXTS = {".h", ".hpp", ".hxx", ".inl", ".cpp", ".cc", ".cxx"}

REL_ID_RE = re.compile(r"REL::ID\(([^()]*)\)")
_DIGITS = re.compile(r"\d+")


# ---------------------------------------------------------------------------
# Exe: .pdata-derived function boundaries + direct call graph, cached on disk.
# ---------------------------------------------------------------------------

class Exe:
    def __init__(self, path):
        self.path = str(Path(path).resolve())
        st = Path(self.path).stat()
        self._key = (self.path, st.st_size, st.st_mtime_ns)
        cache_file = CACHE_DIR / ("xg-" + hashlib.sha1(repr(self._key).encode()).hexdigest() + ".pkl")
        if cache_file.exists():
            with cache_file.open("rb") as f:
                data = pickle.load(f)
        else:
            data = self._build()
            tmp = cache_file.with_suffix(".tmp")
            with tmp.open("wb") as f:
                pickle.dump(data, f, protocol=pickle.HIGHEST_PROTOCOL)
            tmp.replace(cache_file)

        self.base = data["base"]
        self.text_rva = data["text_rva"]
        self.text_end = data["text_end"]
        self.starts = data["starts"]     # sorted non-chained function starts, sentinel = text_end
        self.calls = data["calls"]       # [(site, target_rva_or_None-if-oob, kind), ...] sorted by site
        self.n_funcs = len(self.starts) - 1
        self._build_indices()

    # -- build (uncached path) ------------------------------------------------
    def _build(self):
        pe = pefile.PE(self.path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"]])
        base = pe.OPTIONAL_HEADER.ImageBase
        sections = {s.Name.rstrip(b"\0").decode(errors="replace"): s for s in pe.sections}
        text = sections[".text"]
        text_rva = text.VirtualAddress
        text_bytes = text.get_data()[: text.Misc_VirtualSize]
        text_end = text_rva + len(text_bytes)

        starts = set()
        for e in pe.DIRECTORY_ENTRY_EXCEPTION:
            b = e.struct.BeginAddress
            if not (text_rva <= b < text_end):
                continue
            unwind_rva = e.struct.UnwindData
            flag_byte = pe.get_data(unwind_rva, 1)[0]
            chained = bool((flag_byte >> 3) & 0x4)
            if not chained:
                starts.add(b)
        starts = sorted(starts)
        starts.append(text_end)  # sentinel

        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        md.detail = False
        md.skipdata = True
        calls = []
        for addr, size, mnem, ops in md.disasm_lite(text_bytes, text_rva):
            if size != 5 or not ops.startswith("0x"):
                continue
            if mnem == "call":
                kind = 0
            elif mnem == "jmp":
                kind = 1
            else:
                continue
            target = int(ops, 16)
            if text_rva <= target < text_end:
                calls.append((addr, target, kind))
        calls.sort()

        return {"base": base, "text_rva": text_rva, "text_end": text_end, "starts": starts, "calls": calls}

    # -- derived, rebuilt every load (cheap) ----------------------------------
    def _build_indices(self):
        by_func = {}       # caller_func_start -> [(site, callee_func_start_or_None, kind), ...] ordered by site
        callee_idx = {}     # callee_func_start -> [(caller_func_start, site), ...]
        for site, target, kind in self.calls:
            fi = self.func_index(site)
            if fi is None:
                continue
            caller_start = self.starts[fi]
            ti = self.func_index(target)
            callee_start = self.starts[ti] if ti is not None else None
            by_func.setdefault(caller_start, []).append((site, callee_start, kind))
            if callee_start is not None:
                callee_idx.setdefault(callee_start, []).append((caller_start, site))
        self.by_func = by_func
        self.callee_idx = callee_idx

    def func_index(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0 or i >= len(self.starts) - 1:
            return None
        return i

    def func_start(self, addr):
        i = self.func_index(addr)
        return None if i is None else self.starts[i]

    def func_end(self, start):
        i = bisect.bisect_left(self.starts, start)
        if i >= len(self.starts) - 1 or self.starts[i] != start:
            return None
        return self.starts[i + 1]


# ---------------------------------------------------------------------------
# Anchors: CommonLibF4RD's REL::ID(og, ae) pairs, resolved through the address
# libraries, kept only where both sides land inside a real .text function.
# ---------------------------------------------------------------------------

def _scan_rel_id_pairs():
    pairs = []
    for root in CLIB_ROOTS:
        if not root.exists():
            continue
        for p in root.rglob("*"):
            if p.suffix.lower() not in _SRC_EXTS or not p.is_file():
                continue
            try:
                text = p.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            if "REL::ID(" not in text:
                continue
            for m in REL_ID_RE.finditer(text):
                parts = [t.strip() for t in m.group(1).split(",")]
                if not parts or not all(_DIGITS.fullmatch(t) for t in parts):
                    continue  # REL::ID::INVALID_ID or anything non-numeric: skip
                if len(parts) == 2:
                    pairs.append((int(parts[0]), int(parts[1])))       # (og, ae)
                elif len(parts) == 3:
                    pairs.append((int(parts[0]), int(parts[2])))       # (og, ng, ae) -> (og, ae)
                # len == 1: AE-only, no OG side -> not a cross-runtime anchor
    return pairs


def build_anchors(og: Exe, ae: Exe):
    """-> (dict[og_func_start] = ae_func_start, stats dict). Cached on disk."""
    sig = hashlib.sha1()
    for root in CLIB_ROOTS:
        sig.update(str(root).encode())
        if root.exists():
            for p in sorted(root.rglob("*")):
                if p.suffix.lower() in _SRC_EXTS and p.is_file():
                    st = p.stat()
                    sig.update(f"{p}:{st.st_size}:{st.st_mtime_ns}".encode())
    sig.update(repr(og._key).encode())
    sig.update(repr(ae._key).encode())
    cache_file = CACHE_DIR / ("anchors-" + sig.hexdigest() + ".pkl")
    if cache_file.exists():
        with cache_file.open("rb") as f:
            return pickle.load(f)

    og_table = addrlib.load(OG_BIN)
    ae_table = addrlib.load(AE_BIN)
    raw_pairs = _scan_rel_id_pairs()

    result = {}
    stats = {"raw": len(raw_pairs), "id_resolved_both": 0, "in_text_both": 0, "conflicts": 0}
    for og_id, ae_id in raw_pairs:
        og_off = og_table.get(og_id)
        ae_off = ae_table.get(ae_id)
        if og_off is None or ae_off is None:
            continue
        stats["id_resolved_both"] += 1
        if not (og.text_rva <= og_off < og.text_end) or not (ae.text_rva <= ae_off < ae.text_end):
            continue
        og_fs = og.func_start(og_off)
        ae_fs = ae.func_start(ae_off)
        if og_fs is None or ae_fs is None:
            continue
        stats["in_text_both"] += 1
        if og_fs in result and result[og_fs] != ae_fs:
            stats["conflicts"] += 1
            continue
        result[og_fs] = ae_fs

    data = (result, stats)
    tmp = cache_file.with_suffix(".tmp")
    with tmp.open("wb") as f:
        pickle.dump(data, f, protocol=pickle.HIGHEST_PROTOCOL)
    tmp.replace(cache_file)
    return data


# ---------------------------------------------------------------------------
# Alignment: a monotonic (i, j) backbone between an OG call-sequence and its
# AE counterpart, pinned by unambiguous anchored callees ("patience diff").
# ---------------------------------------------------------------------------

def _landmarks(og_calls, ae_calls, anchor_map, exclude_og=None, exclude_ae=None):
    ae_pos_by_callee = {}
    for j, (_site, callee, _k) in enumerate(ae_calls):
        if callee is not None:
            ae_pos_by_callee.setdefault(callee, []).append(j)

    candidates = []  # (i, j), i strictly increasing by construction (one per og_calls index)
    for i, (_site, callee, _k) in enumerate(og_calls):
        if callee is None or callee == exclude_og:
            continue
        ae_target = anchor_map.get(callee)
        if ae_target is None or ae_target == exclude_ae:
            continue
        js = ae_pos_by_callee.get(ae_target)
        if not js or len(js) != 1:
            continue  # ambiguous on the AE side (callee called >1 time): not a safe landmark
        candidates.append((i, js[0]))

    if not candidates:
        return []

    # longest strictly-increasing-in-j subsequence (i is already increasing) = patience LIS
    js_seq = [c[1] for c in candidates]
    tails_val, tails_idx, prev = [], [], [-1] * len(candidates)
    for idx, j in enumerate(js_seq):
        pos = bisect.bisect_left(tails_val, j)
        if pos == len(tails_val):
            tails_val.append(j)
            tails_idx.append(idx)
        else:
            tails_val[pos] = j
            tails_idx[pos] = idx
        prev[idx] = tails_idx[pos - 1] if pos > 0 else -1
    chain = []
    k = tails_idx[-1]
    while k != -1:
        chain.append(candidates[k])
        k = prev[k]
    chain.reverse()
    return chain


def _interp(chain, p):
    """Estimate the AE-sequence index aligned to OG-sequence index p, from a monotonic
    (i, j) backbone. Extrapolates by a flat offset past either end of the chain."""
    if not chain:
        return None
    lo = hi = None
    for i, j in chain:
        if i <= p:
            lo = (i, j)
        if i >= p and hi is None:
            hi = (i, j)
    if lo is None:
        i, j = hi
        return j - (i - p)
    if hi is None:
        i, j = lo
        return j + (p - i)
    if lo[0] == hi[0]:
        return lo[1]
    i0, j0 = lo
    i1, j1 = hi
    frac = (p - i0) / (i1 - i0)
    return round(j0 + frac * (j1 - j0))


# ---------------------------------------------------------------------------
# match() / callsite()
# ---------------------------------------------------------------------------

def match(og: Exe, ae: Exe, anchor_map: dict, og_addr: int, exclude_og=None, exclude_ae=None):
    """Rank AE candidates for the OG function containing `og_addr`, by call-graph
    anchoring through OG's callers. `exclude_og`/`exclude_ae` hide one anchor pair
    from every vote/landmark computation (used by selftest to avoid leaking the
    answer it is trying to recover)."""
    og_target = og.func_start(og_addr)
    if og_target is None:
        return {"error": f"{og_addr:#x} is not inside any OG .text function"}

    callers = og.callee_idx.get(og_target, [])
    votes = {}
    per_caller = []
    for caller_start, site in callers:
        entry = {"caller": caller_start, "site": site}
        if caller_start == exclude_og:
            entry["skipped"] = "caller is the held-out function itself (self/recursive call)"
            per_caller.append(entry)
            continue
        ae_caller = anchor_map.get(caller_start)
        entry["caller_anchored"] = ae_caller is not None
        if ae_caller is None:
            entry["skipped"] = "caller not anchored"
            per_caller.append(entry)
            continue
        og_calls = og.by_func.get(caller_start, [])
        ae_calls = ae.by_func.get(ae_caller, [])
        p = next((idx for idx, c in enumerate(og_calls) if c[0] == site), None)
        if p is None:
            entry["skipped"] = "call site not found in caller's own call list (bug guard)"
            per_caller.append(entry)
            continue
        chain = _landmarks(og_calls, ae_calls, anchor_map, exclude_og=exclude_og, exclude_ae=exclude_ae)
        entry["landmarks"] = len(chain)
        j = _interp(chain, p)
        if j is None or not (0 <= j < len(ae_calls)):
            entry["skipped"] = "no usable alignment landmarks in this caller"
            per_caller.append(entry)
            continue
        candidate = ae_calls[j][1]
        entry["aligned_ae_index"] = j
        entry["aligned_ae_site"] = ae_calls[j][0]
        entry["candidate"] = candidate
        if candidate is None:
            entry["skipped"] = "aligned AE call target is not inside a known AE function"
            per_caller.append(entry)
            continue
        votes[candidate] = votes.get(candidate, 0) + 1
        per_caller.append(entry)

    og_callees = og.by_func.get(og_target, [])
    anchored_callees = [
        (c, anchor_map[c]) for _s, c, _k in og_callees if c is not None and c in anchor_map and c != exclude_og
    ]
    confirmations = {}
    for cand in votes:
        ae_callee_set = {c for _s, c, _k in ae.by_func.get(cand, []) if c is not None}
        hit = sum(1 for _og_c, ae_c in anchored_callees if ae_c in ae_callee_set)
        confirmations[cand] = (hit, len(anchored_callees))

    ranked = sorted(votes.items(), key=lambda kv: (-kv[1], -confirmations.get(kv[0], (0, 0))[0]))
    return {
        "og_target": og_target,
        "callers_total": len(callers),
        "callers_anchored": sum(1 for e in per_caller if e.get("caller_anchored")),
        "callers_voted": sum(1 for e in per_caller if "candidate" in e),
        "votes": ranked,
        "confirmations": confirmations,
        "per_caller": per_caller,
    }


def callsite(og: Exe, ae: Exe, anchor_map: dict, og_site: int):
    caller_start = og.func_start(og_site)
    if caller_start is None:
        return {"error": f"{og_site:#x} is not inside any OG .text function"}
    og_calls = og.by_func.get(caller_start, [])
    p = next((idx for idx, c in enumerate(og_calls) if c[0] == og_site), None)
    if p is None:
        return {"error": f"{og_site:#x} is not a recorded direct call/jmp instruction (E8/E9 rel32 into .text)"}
    og_callee = og_calls[p][1]

    ae_caller = anchor_map.get(caller_start)
    if ae_caller is not None:
        via = "containing function is a direct REL::ID anchor"
        match_res = None
    else:
        match_res = match(og, ae, anchor_map, caller_start)
        ranked = match_res.get("votes") or []
        if not ranked:
            return {
                "error": "containing OG function is not anchored, and match() found no AE candidate for it",
                "og_caller": caller_start,
                "match": match_res,
            }
        ae_caller = ranked[0][0]
        tie = len(ranked) > 1 and ranked[1][1] == ranked[0][1]
        via = f"match() top candidate ({ranked[0][1]} votes{', TIED with runner-up' if tie else ''})"

    ae_calls = ae.by_func.get(ae_caller, [])
    chain = _landmarks(og_calls, ae_calls, anchor_map)
    j = _interp(chain, p)
    if j is None or not (0 <= j < len(ae_calls)):
        return {
            "error": "no alignment landmarks inside the (candidate) containing AE function",
            "og_caller": caller_start,
            "ae_caller": ae_caller,
            "via": via,
        }
    ae_site, ae_callee, _kind = ae_calls[j]
    return {
        "og_caller": caller_start,
        "og_site": og_site,
        "og_callee": og_callee,
        "ae_caller": ae_caller,
        "ae_site": ae_site,
        "ae_callee": ae_callee,
        "via": via,
        "landmarks": len(chain),
        "aligned_ae_index": j,
    }


# ---------------------------------------------------------------------------
# selftest
# ---------------------------------------------------------------------------

def selftest(og: Exe, ae: Exe, anchor_map: dict, n=200, seed=1234):
    eligible = [fs for fs in anchor_map if og.callee_idx.get(fs)]
    rnd = random.Random(seed)
    sample = rnd.sample(eligible, min(n, len(eligible)))

    top1 = ambiguous = wrong = none = 0
    wrong_examples = []
    for og_fs in sample:
        true_ae = anchor_map[og_fs]
        res = match(og, ae, anchor_map, og_fs, exclude_og=og_fs, exclude_ae=true_ae)
        ranked = res["votes"]
        if not ranked:
            none += 1
            continue
        top_score = ranked[0][1]
        tied = [c for c, v in ranked if v == top_score]
        if len(tied) > 1:
            ambiguous += 1
            continue
        top = ranked[0][0]
        if top == true_ae:
            top1 += 1
        else:
            wrong += 1
            if len(wrong_examples) < 12:
                wrong_examples.append((og_fs, true_ae, top, top_score))

    return {
        "n": len(sample),
        "eligible_pool": len(eligible),
        "top1": top1,
        "ambiguous": ambiguous,
        "wrong": wrong,
        "none": none,
        "wrong_examples": wrong_examples,
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _load():
    og = Exe(OG_EXE)
    ae = Exe(AE_EXE)
    anchor_map, stats = build_anchors(og, ae)
    return og, ae, anchor_map, stats


def _print_anchor_stats(stats):
    print(
        f"[anchors] REL::ID pairs found: {stats['raw']}  "
        f"both ids resolved in address libs: {stats['id_resolved_both']}  "
        f"usable (both land in a .text function): {stats['in_text_both']}  "
        f"conflicts (same OG fn, different AE fn seen twice): {stats['conflicts']}"
    )


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_fn = sub.add_parser("fn", help="find the AE counterpart of an OG function")
    p_fn.add_argument("addr", help="OG address (hex), inside or at the start of the function")
    p_site = sub.add_parser("site", help="find the AE counterpart of an OG call/jmp site")
    p_site.add_argument("addr", help="OG address (hex) of a direct call/jmp instruction")
    p_st = sub.add_parser("selftest", help="hide N real REL::ID anchors and try to recover them")
    p_st.add_argument("n", nargs="?", type=int, default=200)
    p_st.add_argument("--seed", type=int, default=1234)
    a = ap.parse_args(argv)

    og, ae, anchor_map, stats = _load()
    _print_anchor_stats(stats)

    if a.cmd == "selftest":
        r = selftest(og, ae, anchor_map, n=a.n, seed=a.seed)
        print(f"\nselftest: n={r['n']} (of {r['eligible_pool']} anchored OG functions with >=1 anchored caller)")
        print(f"  top-1 correct    : {r['top1']}")
        print(f"  ambiguous (tied) : {r['ambiguous']}  (NOT counted as correct)")
        print(f"  wrong-but-confident: {r['wrong']}")
        print(f"  no candidate     : {r['none']}")
        if r["wrong_examples"]:
            print("  wrong-but-confident examples (OG fn -> true AE fn, got AE fn, votes):")
            for og_fs, true_ae, got, v in r["wrong_examples"]:
                print(f"    OG {og_fs:#x} -> true AE {true_ae:#x}, got AE {got:#x} ({v} votes)")
        return 0

    if a.cmd == "fn":
        target = int(a.addr, 16)
        res = match(og, ae, anchor_map, target)
        if "error" in res:
            print(f"\nOG {target:#x}: {res['error']}")
            return 1
        snap = "" if res["og_target"] == target else f" (snapped to function start, given addr was {target:#x} into it)"
        print(f"\nOG function {res['og_target']:#x}{snap}")
        print(f"  callers: {res['callers_total']} total, {res['callers_anchored']} anchored, {res['callers_voted']} produced a vote")
        if not res["votes"]:
            print("  NO CANDIDATE")
        else:
            for ae_fs, v in res["votes"]:
                hit, total = res["confirmations"].get(ae_fs, (0, 0))
                print(f"  candidate AE {ae_fs:#x}   votes={v}   callee-confirmations={hit}/{total}")
        print("  evidence per caller:")
        for e in res["per_caller"]:
            print(f"    {e}")
        return 0

    if a.cmd == "site":
        target = int(a.addr, 16)
        res = callsite(og, ae, anchor_map, target)
        if "error" in res:
            print(f"\nOG site {target:#x}: {res['error']}")
            if res.get("match"):
                print(f"  match() detail: {res['match']}")
            return 1
        print(f"\nOG site {res['og_site']:#x} in function {res['og_caller']:#x} -> OG callee {res['og_callee']:#x}")
        print(f"  AE caller {res['ae_caller']:#x}  (via: {res['via']})")
        print(f"  aligned using {res['landmarks']} landmark(s), AE index {res['aligned_ae_index']}")
        callee_s = f"{res['ae_callee']:#x}" if res["ae_callee"] is not None else "(target not inside a known AE function)"
        print(f"  AE call site {res['ae_site']:#x} -> AE callee {callee_s}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
