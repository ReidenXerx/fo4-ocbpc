# SPDX-License-Identifier: GPL-3.0-only
"""Every AE (or OG) logical function containing ALL of a set of operand patterns: a function found by what it does.
Capstone over the whole .text once (cached as per-instruction text by function root), then substring queries.

    python tools/rd/opscan.py "*4 + 0x1c8]" "*4 + 0x18]" "+ 0xf0]"            # AE by default
    python tools/rd/opscan.py --exe og "sub rsp, 0x810"
"""
import argparse
import bisect
import pickle
import sys

import capstone

import fnroot
import xmatch

EXES = {'og': fnroot.OG, 'ae': xmatch.AE_EXE}


def load(which):
    cache = xmatch.CACHE_DIR / f'opscan-{which}.pkl'
    if cache.exists():
        return pickle.loads(cache.read_bytes())
    fx = fnroot.Functions(EXES[which])
    import pefile
    pe = pefile.PE(EXES[which], fast_load=True)
    text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    base, data = text.VirtualAddress, bytes(fx.image[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.skipdata = True
    # root of every .pdata entry, so an instruction maps to its logical function
    entries = sorted((b, e, u) for b, e, u in fx.rf)
    begins = [b for b, _, _ in entries]
    roots = [fx.root(ent) for ent in entries]
    lines = {}
    for addr, size, mnem, ops in md.disasm_lite(data, base):
        i = bisect.bisect_right(begins, addr) - 1
        if i < 0 or not (entries[i][0] <= addr < entries[i][1]) or not roots[i]:
            continue
        lines.setdefault(roots[i][0], []).append(f'{addr:#x} {mnem} {ops}')
    cache.write_bytes(pickle.dumps(lines))
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('patterns', nargs='+')
    ap.add_argument('--exe', default='ae', choices=EXES)
    ap.add_argument('--show', type=int, default=3, help='matching lines to show per pattern')
    a = ap.parse_args()
    lines = load(a.exe)
    hits = []
    for root, ls in lines.items():
        found = {p: [l for l in ls if p in l] for p in a.patterns}
        if all(found.values()):
            hits.append((root, len(ls), found))
    print(f'{len(hits)} function(s) in {a.exe} contain all of {a.patterns}')
    for root, n, found in sorted(hits)[:20]:
        print(f'  root {root:#x} ({n} instructions)')
        for p, ls in found.items():
            print(f'     {p!r}: {len(ls)}: ' + ' | '.join(ls[:a.show]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
