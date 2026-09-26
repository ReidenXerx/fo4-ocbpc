#!/usr/bin/env python3
"""Python 3.12 reader for CommonLibF4RD's `f4rd-runtime.bin`.

Faithful port of the binary format read by:
  CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp
    - RuntimeDatabase::load()          (file layout / header / tables)
    - RuntimeDatabase::find()          (id -> record, alias resolution + version scoping)
    - RuntimeDatabase::contains_id()
    - RuntimeDatabase::known_mappings()
  CommonLibF4RD/CommonLibF4/src/REL/Relocation.cpp
    - IDDatabase::load()               (embedded/legacy OG {id,offset} table detection)
    - IDDatabase::resolve(const ID&)   ("automatic bridge": AE id resolved through the RD
                                         first; on failure falls back to a hard-coded OG id)

DELIBERATE SCOPE CUT (see MISSED in the handback report): this reader only decodes the
Record / Alias / KnownRVA tables. It does NOT decode Candidate / Fragment / Constraint /
pattern-blob data - a record's "has patterns" is read directly from its own
`candidateCount` field (RECORD_SIZE offset +28), which is exactly what
RuntimeDatabase::pattern_ids() keys off of (`read_le<uint32_t>(bytes, recordOffset + 28)
== 0` -> skip). Nothing about identity/version-mapping requires the pattern bytes.

It also does NOT re-run RuntimeDatabase::load()'s full one-time structural validation
(the `validate_mapped_database_once` block) on every parse - that block exists in the
C++ purely to amortize an expensive check across process launches via a named shared
memory flag; a batch CLI tool re-parsing on every invocation would just re-pay the
full cost every time. We do keep the CHEAP structural checks (magic, format version,
header size, endian marker, table-offset sanity, reserved-must-be-zero fields hit while
we're already reading the row) and the CRC32 check is available via --verify-crc.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import os
import struct
import sys
import time
import zlib
from array import array
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Constants - ported 1:1 from RuntimeDatabase.cpp's anonymous namespace.
# ---------------------------------------------------------------------------

MAGIC = b"F4RDBIN\x00"                       # RuntimeDatabase.cpp:10
ENDIAN_MARKER = 0x01020304                   # RuntimeDatabase.cpp:11
HEADER_SIZE = 112                            # RuntimeDatabase.cpp:12
RECORD_SIZE = 40                             # RuntimeDatabase.cpp:13
ALIAS_SIZE = 24                              # RuntimeDatabase.cpp:14
KNOWN_RVA_SIZE = 16                          # RuntimeDatabase.cpp:15
ALIAS_VERSION_MAJOR_MINOR = 1                # RuntimeDatabase.cpp:34
LEGACY_RECORD_SIZE = 16                      # RuntimeDatabase.cpp:871 (sizeof(uint64_t) * 2)

RECORD_STRUCT = struct.Struct("<Q8I")        # id, firstAlias, aliasCount, firstKnown,
                                              # knownCount, firstCandidate, candidateCount,
                                              # flags, reserved0   (RuntimeDatabase.cpp:1200-1210)
ALIAS_STRUCT = struct.Struct("<Q4HII")       # id, version[4], recordIndex, flags
                                              # (RuntimeDatabase.cpp:1226-1231)
KNOWN_STRUCT = struct.Struct("<4HII")        # version[4], rva, flags(must==0)
                                              # (RuntimeDatabase.cpp:1242-1246)
LEGACY_STRUCT = struct.Struct("<QQ")         # id, offset   (Relocation.cpp mapping_t)

VersionTuple = tuple  # (major, minor, build, sub) of int


# ---------------------------------------------------------------------------
# Version-family / scoping logic - ported from RuntimeDatabase.cpp:62-91.
# ---------------------------------------------------------------------------

def runtime_family_key(version: VersionTuple) -> str:
    """RuntimeDatabase.cpp:69-79 runtime_family_key()."""
    if version >= (1, 11, 0, 0):
        return "AE"
    if version >= (1, 10, 980, 0):
        return "NG"
    return "OG"


def version_scope_matches(source: VersionTuple, target: VersionTuple, major_minor: bool) -> bool:
    """RuntimeDatabase.cpp:81-91 version_scope_matches()."""
    if not major_minor:
        return source == target
    return (
        source[0] == target[0]
        and source[1] == target[1]
        and runtime_family_key(source) == runtime_family_key(target)
    )


def parse_version(text: str) -> VersionTuple:
    parts = [int(x) for x in text.split(".") if x != ""]
    if not parts or len(parts) > 4:
        raise ValueError(f"invalid version string: {text!r}")
    while len(parts) < 4:
        parts.append(0)
    return tuple(parts)


def format_version(v: VersionTuple) -> str:
    return ".".join(str(x) for x in v)


# ---------------------------------------------------------------------------
# Database container - Structure-of-Arrays, mirrors the on-disk tables so a
# 782K-record / 1.8M-knownRVA file (the measured real one) stays cheap to hold
# and to pickle, instead of ~800K Python objects.
# ---------------------------------------------------------------------------

@dataclass
class RuntimeDatabase:
    source_path: str = ""
    file_size: int = 0
    db_offset: int = 0                       # 0 unless a legacy prefix precedes F4RDBIN
    format_minor: int = 0
    payload_crc: int = 0
    crc_verified: bool = False

    record_count: int = 0
    alias_count: int = 0
    known_count: int = 0
    candidate_count: int = 0
    fragment_count: int = 0
    constraint_count: int = 0

    # Record table (SoA), index 0..record_count-1, sorted ascending by id
    # (RuntimeDatabase.cpp enforces `previousID < id` in its one-time validator).
    record_ids: array = field(default_factory=lambda: array("Q"))
    record_flags: array = field(default_factory=lambda: array("I"))
    record_first_alias: array = field(default_factory=lambda: array("I"))
    record_alias_count: array = field(default_factory=lambda: array("I"))
    record_first_known: array = field(default_factory=lambda: array("I"))
    record_known_count: array = field(default_factory=lambda: array("I"))
    record_candidate_count: array = field(default_factory=lambda: array("I"))

    # Alias table (SoA). alias_version is flattened 4x uint16 per row.
    alias_id: array = field(default_factory=lambda: array("Q"))
    alias_version: array = field(default_factory=lambda: array("H"))
    alias_record_index: array = field(default_factory=lambda: array("I"))
    alias_flags: array = field(default_factory=lambda: array("I"))

    # KnownRVA table (SoA). known_version is flattened 4x uint16 per row.
    known_version: array = field(default_factory=lambda: array("H"))
    known_rva: array = field(default_factory=lambda: array("I"))

    # Embedded/legacy OG {id -> offset} table read straight off disk ahead of
    # the F4RDBIN payload (Relocation.cpp's loadEmbeddedOGTable). Sorted
    # ascending by id (format requires strictly increasing ids).
    legacy_ids: array = field(default_factory=lambda: array("Q"))
    legacy_offsets: array = field(default_factory=lambda: array("Q"))

    # id -> list[row index into alias_* arrays]. Built once at parse time;
    # cheap because alias_count is small (12,446 on the measured real file
    # vs. 782,168 records).
    _alias_by_id: dict = field(default_factory=dict)

    # (version_tuple, rva) -> list[record index]. Built once at parse time so
    # `rva <version> <hex>` is an O(1) dict lookup instead of an O(knownCount)
    # scan on every CLI invocation.
    _known_index: dict = field(default_factory=dict)

    # ---- lookups -----------------------------------------------------

    def record_index_by_canonical_id(self, id_: int):
        """Binary search on the sorted record-id table."""
        lo = bisect.bisect_left(self.record_ids, id_)
        if lo < len(self.record_ids) and self.record_ids[lo] == id_:
            return lo
        return None

    def find_record_index(self, id_: int, version: VersionTuple):
        """Port of RuntimeDatabase::find() (RuntimeDatabase.cpp:1512-1561).

        Alias resolution wins over an equal canonical id from another runtime
        family (OG and AE/NG use independent numeric id spaces - see the
        comment on the C++ side), and an exact-version alias wins over a
        scoped (major.minor + family) one.
        """
        rows = self._alias_by_id.get(id_)
        if rows:
            for row in rows:  # pass 1: exact-version aliases (flags bit clear)
                if not (self.alias_flags[row] & ALIAS_VERSION_MAJOR_MINOR):
                    v = tuple(self.alias_version[row * 4:row * 4 + 4])
                    if version_scope_matches(v, version, False):
                        return self.alias_record_index[row]
            for row in rows:  # pass 2: scoped aliases (flags bit set)
                if self.alias_flags[row] & ALIAS_VERSION_MAJOR_MINOR:
                    v = tuple(self.alias_version[row * 4:row * 4 + 4])
                    if version_scope_matches(v, version, True):
                        return self.alias_record_index[row]
        return self.record_index_by_canonical_id(id_)

    def contains_id(self, id_: int, version: VersionTuple) -> bool:
        """Port of RuntimeDatabase::contains_id()."""
        return self.find_record_index(id_, version) is not None

    def record_indices_for_id_any_version(self, id_: int):
        """Every distinct record this id reaches, ignoring version scope
        (used by the `id` / `bridge` CLI commands, which take no version)."""
        idx = self.record_index_by_canonical_id(id_)
        indices = {idx} if idx is not None else set()
        for row in self._alias_by_id.get(id_, ()):
            indices.add(self.alias_record_index[row])
        return sorted(indices)

    def record_view(self, index: int) -> dict:
        rid = self.record_ids[index]
        flags = self.record_flags[index]
        fa, ac = self.record_first_alias[index], self.record_alias_count[index]
        fk, kc = self.record_first_known[index], self.record_known_count[index]
        cc = self.record_candidate_count[index]
        aliases = []
        for i in range(fa, fa + ac):
            v = tuple(self.alias_version[i * 4:i * 4 + 4])
            af = self.alias_flags[i]
            aliases.append({
                "id": self.alias_id[i],
                "version": v,
                "flags": af,
                "scoped": bool(af & ALIAS_VERSION_MAJOR_MINOR),
            })
        known_rvas = []
        for i in range(fk, fk + kc):
            v = tuple(self.known_version[i * 4:i * 4 + 4])
            known_rvas.append({"version": v, "rva": self.known_rva[i]})
        return {
            "index": index,
            "id": rid,
            "flags": flags,
            "aliases": aliases,
            "known_rvas": known_rvas,
            "candidate_count": cc,
            "has_patterns": cc > 0,
        }

    def records(self):
        """records(): yields the record_view() dict for every record in the
        database (id, aliases w/ id+version+flags, known RVAs per version,
        has_patterns)."""
        for i in range(self.record_count):
            yield self.record_view(i)

    def record_for_version(self, id_: int, version: VersionTuple):
        """For a version string and an id: the record, plus every
        alias/known-RVA it has for OTHER versions (record_view() always
        returns the full alias/knownRVA lists, unfiltered by version, so the
        cross-runtime mapping falls out for free once the right record is
        resolved for THIS version)."""
        idx = self.find_record_index(id_, version)
        return None if idx is None else self.record_view(idx)

    def rva_lookup(self, version: VersionTuple, rva: int):
        """rva <version> <hex>: every record whose known RVA for that exact
        version equals rva. Backed by the reverse index built at parse time."""
        indices = self._known_index.get((version, rva), [])
        return [self.record_view(i) for i in indices]

    def bridge(self, ae_id: int):
        """The 'verified automatic bridge' (Relocation.cpp IDDatabase::resolve
        (const ID&), lines ~875-911): for an OG-family module, an id's AE
        number is tried against the runtime database FIRST (no hard-coded OG
        id needed - "automatic"); only when that fails does the code fall
        back to a hard-coded classic OG id, either through the embedded
        legacy OG table or a second runtime-database lookup keyed by that OG
        id. This method has no live module/version to resolve *for*, so it
        answers the static question instead: which other ids (OG/NG) does
        the runtime database record for `ae_id` carry as aliases, i.e. what
        would the bridge resolve to. Also reports if the id shows up in the
        embedded legacy OG {id,offset} table directly."""
        record_indices = self.record_indices_for_id_any_version(ae_id)
        legacy_hit = None
        lo = bisect.bisect_left(self.legacy_ids, ae_id)
        if lo < len(self.legacy_ids) and self.legacy_ids[lo] == ae_id:
            legacy_hit = self.legacy_offsets[lo]
        return {
            "queried_id": ae_id,
            "records": [self.record_view(i) for i in record_indices],
            "legacy_og_table_offset": legacy_hit,
        }

    def distinct_versions(self):
        """Every exact version present in the known-RVA table (i.e. every
        version the database actually carries per-version data for)."""
        return sorted({key[0] for key in self._known_index.keys()})


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

def _read_exact(f, offset: int, size: int) -> bytes:
    f.seek(offset)
    data = f.read(size)
    if len(data) != size:
        raise ValueError(f"runtime database is truncated (wanted {size} bytes at {offset}, got {len(data)})")
    return data


def _parse(path: str, verify_crc: bool = False, progress: bool = False) -> RuntimeDatabase:
    db = RuntimeDatabase()
    db.source_path = os.path.abspath(path)
    db.file_size = os.path.getsize(path)

    with open(path, "rb") as f:
        first8 = _read_exact(f, 0, 8)

        # --- legacy-prefix detection (RuntimeDatabase.cpp:895-921) ------
        # If the file doesn't open directly on the F4RDBIN magic, the first
        # 8 bytes are a little-endian count of {id,offset} pairs (16 bytes
        # each); the F4RDBIN payload must begin immediately after them.
        db_offset = 0
        if first8 != MAGIC:
            legacy_count = struct.unpack_from("<Q", first8, 0)[0]
            db_offset = 8 + legacy_count * LEGACY_RECORD_SIZE
            if db_offset > db.file_size or db_offset + 8 > db.file_size:
                raise ValueError("runtime database magic is invalid")
            appended = _read_exact(f, db_offset, 8)
            if appended != MAGIC:
                raise ValueError("runtime database magic is invalid")

            # This is also the "embedded OG legacy table" Relocation.cpp's
            # loadEmbeddedOGTable() reads directly (by re-opening the same
            # file and refusing it if it opens on the magic - i.e. exactly
            # this branch is what it is designed to read).
            if progress:
                print(f"[f4rd_db] legacy OG table: {legacy_count} entries", file=sys.stderr)
            legacy_bytes = _read_exact(f, 8, legacy_count * LEGACY_RECORD_SIZE)
            prev_id = None
            ids = array("Q")
            offs = array("Q")
            for eid, eoff in LEGACY_STRUCT.iter_unpack(legacy_bytes):
                if prev_id is not None and prev_id >= eid:
                    raise ValueError("embedded OG legacy table ids are not strictly ascending")
                ids.append(eid)
                offs.append(eoff)
                prev_id = eid
            db.legacy_ids = ids
            db.legacy_offsets = offs
        db.db_offset = db_offset

        header = _read_exact(f, db_offset, HEADER_SIZE)
        fmt_major, fmt_minor = struct.unpack_from("<HH", header, 8)
        header_size_field = struct.unpack_from("<I", header, 12)[0]
        reserved0 = struct.unpack_from("<I", header, 16)[0]
        endian_marker = struct.unpack_from("<I", header, 20)[0]
        runtime_size = db.file_size - db_offset
        total_size_field = struct.unpack_from("<Q", header, 96)[0]
        reserved1 = struct.unpack_from("<I", header, 108)[0]
        if (
            fmt_major != 1
            or fmt_minor > 5
            or header_size_field != HEADER_SIZE
            or reserved0 != 0
            or endian_marker != ENDIAN_MARKER
            or total_size_field != runtime_size
            or reserved1 != 0
        ):
            raise ValueError("runtime database header is unsupported")
        db.format_minor = fmt_minor
        db.payload_crc = struct.unpack_from("<I", header, 104)[0]

        (record_count, alias_count, known_count,
         candidate_count, fragment_count) = struct.unpack_from("<IIIII", header, 24)
        constraint_count = struct.unpack_from("<I", header, 44)[0] if fmt_minor >= 1 else 0

        (records_offset, aliases_offset, known_offset,
         candidates_offset, fragments_offset, blob_offset) = struct.unpack_from("<QQQQQQ", header, 48)

        if records_offset != HEADER_SIZE:
            raise ValueError("runtime database record table is misplaced")
        for name, off in (("aliases", aliases_offset), ("known", known_offset),
                          ("candidates", candidates_offset), ("fragments", fragments_offset),
                          ("blob", blob_offset)):
            if off > runtime_size:
                raise ValueError(f"runtime database {name} offset is out of range")

        db.record_count = record_count
        db.alias_count = alias_count
        db.known_count = known_count
        db.candidate_count = candidate_count
        db.fragment_count = fragment_count
        db.constraint_count = constraint_count

        if verify_crc:
            if progress:
                print("[f4rd_db] verifying payload CRC32 (this reads the whole payload)...", file=sys.stderr)
            f.seek(db_offset + HEADER_SIZE)
            crc = 0
            remaining = runtime_size - HEADER_SIZE
            chunk_size = 16 * 1024 * 1024
            while remaining > 0:
                chunk = f.read(min(chunk_size, remaining))
                if not chunk:
                    break
                crc = zlib.crc32(chunk, crc)
                remaining -= len(chunk)
            db.crc_verified = (crc & 0xFFFFFFFF) == db.payload_crc
            if not db.crc_verified:
                raise ValueError(
                    f"runtime database payload checksum mismatch "
                    f"(header says 0x{db.payload_crc:08X}, computed 0x{crc & 0xFFFFFFFF:08X})"
                )

        # --- record table --------------------------------------------
        if progress:
            print(f"[f4rd_db] records: {record_count}", file=sys.stderr)
        record_bytes = _read_exact(f, db_offset + records_offset, record_count * RECORD_SIZE)
        ids = array("Q")
        flags_a = array("I")
        first_alias = array("I")
        alias_cnt = array("I")
        first_known = array("I")
        known_cnt = array("I")
        cand_cnt = array("I")
        prev_id = None
        for id_, fa, ac, fk, kc, fc, cc, fl, reserved in RECORD_STRUCT.iter_unpack(record_bytes):
            if reserved != 0:
                raise ValueError("runtime database record reserved field is nonzero")
            if prev_id is not None and prev_id >= id_:
                raise ValueError("runtime database record ids are not strictly ascending")
            ids.append(id_)
            first_alias.append(fa)
            alias_cnt.append(ac)
            first_known.append(fk)
            known_cnt.append(kc)
            cand_cnt.append(cc)
            flags_a.append(fl)
            prev_id = id_
        db.record_ids = ids
        db.record_flags = flags_a
        db.record_first_alias = first_alias
        db.record_alias_count = alias_cnt
        db.record_first_known = first_known
        db.record_known_count = known_cnt
        db.record_candidate_count = cand_cnt
        del record_bytes

        # --- alias table -----------------------------------------------
        if progress:
            print(f"[f4rd_db] aliases: {alias_count}", file=sys.stderr)
        alias_bytes = _read_exact(f, db_offset + aliases_offset, alias_count * ALIAS_SIZE)
        a_ids = array("Q")
        a_versions = array("H")
        a_recidx = array("I")
        a_flags = array("I")
        alias_by_id: dict = {}
        for row, (id_, v0, v1, v2, v3, ridx, fl) in enumerate(ALIAS_STRUCT.iter_unpack(alias_bytes)):
            if ridx >= record_count or (fl & ~ALIAS_VERSION_MAJOR_MINOR) != 0:
                raise ValueError("runtime database alias record is invalid")
            a_ids.append(id_)
            a_versions.extend((v0, v1, v2, v3))
            a_recidx.append(ridx)
            a_flags.append(fl)
            alias_by_id.setdefault(id_, []).append(row)
        db.alias_id = a_ids
        db.alias_version = a_versions
        db.alias_record_index = a_recidx
        db.alias_flags = a_flags
        db._alias_by_id = alias_by_id
        del alias_bytes

        # --- known-RVA table ---------------------------------------------
        if progress:
            print(f"[f4rd_db] known RVAs: {known_count}", file=sys.stderr)
        known_bytes = _read_exact(f, db_offset + known_offset, known_count * KNOWN_RVA_SIZE)
        k_versions = array("H")
        k_rvas = array("I")
        for v0, v1, v2, v3, rva, fl in KNOWN_STRUCT.iter_unpack(known_bytes):
            if fl != 0:
                raise ValueError("runtime database known RVA flags are invalid")
            k_versions.extend((v0, v1, v2, v3))
            k_rvas.append(rva)
        db.known_version = k_versions
        db.known_rva = k_rvas
        del known_bytes

        # --- reverse index for `rva` lookups -----------------------------
        known_index: dict = {}
        for i in range(record_count):
            fk = db.record_first_known[i]
            kc = db.record_known_count[i]
            for k in range(fk, fk + kc):
                v = tuple(db.known_version[k * 4:k * 4 + 4])
                known_index.setdefault((v, db.known_rva[k]), []).append(i)
        db._known_index = known_index

    return db


# ---------------------------------------------------------------------------
# Cache
# ---------------------------------------------------------------------------

def _cache_dir() -> str:
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".cache")
    os.makedirs(d, exist_ok=True)
    return d


def _cache_path(path: str, verify_crc: bool) -> str:
    st = os.stat(path)
    key = f"{os.path.abspath(path)}|{st.st_size}|{int(st.st_mtime)}|verify_crc={verify_crc}|v1"
    digest = hashlib.sha1(key.encode("utf-8")).hexdigest()
    return os.path.join(_cache_dir(), f"f4rd_db_{digest}.pkl")


def load(path: str, use_cache: bool = True, verify_crc: bool = False, progress: bool = False) -> RuntimeDatabase:
    import pickle

    cache_file = _cache_path(path, verify_crc)
    if use_cache and os.path.exists(cache_file):
        try:
            with open(cache_file, "rb") as f:
                db = pickle.load(f)
            if db.source_path == os.path.abspath(path) and db.file_size == os.path.getsize(path):
                if progress:
                    print(f"[f4rd_db] loaded from cache: {cache_file}", file=sys.stderr)
                return db
        except Exception as exc:  # noqa: BLE001 - corrupt/incompatible cache is not fatal
            if progress:
                print(f"[f4rd_db] cache load failed ({exc}); reparsing", file=sys.stderr)

    t0 = time.time()
    db = _parse(path, verify_crc=verify_crc, progress=progress)
    if progress:
        print(f"[f4rd_db] parsed in {time.time() - t0:.1f}s", file=sys.stderr)

    if use_cache:
        tmp = cache_file + ".tmp"
        with open(tmp, "wb") as f:
            pickle.dump(db, f, protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, cache_file)
        if progress:
            print(f"[f4rd_db] cached to {cache_file}", file=sys.stderr)
    return db


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _print_record(db: RuntimeDatabase, rec: dict, indent: str = "") -> None:
    print(f"{indent}record id={rec['id']} (index {rec['index']}) flags=0x{rec['flags']:X} "
          f"has_patterns={rec['has_patterns']} (candidateCount={rec['candidate_count']})")
    if rec["aliases"]:
        print(f"{indent}  aliases:")
        for a in rec["aliases"]:
            scope = "scoped(major.minor+family)" if a["scoped"] else "exact-version"
            fam = runtime_family_key(a["version"])
            print(f"{indent}    id={a['id']:<12} version={format_version(a['version']):<14} "
                  f"family~{fam:<2} {scope} flags=0x{a['flags']:X}")
    else:
        print(f"{indent}  aliases: (none)")
    if rec["known_rvas"]:
        print(f"{indent}  known RVAs:")
        for k in sorted(rec["known_rvas"], key=lambda x: x["version"]):
            fam = runtime_family_key(k["version"])
            print(f"{indent}    version={format_version(k['version']):<14} family~{fam:<2} rva=0x{k['rva']:X}")
    else:
        print(f"{indent}  known RVAs: (none)")


def cmd_id(db: RuntimeDatabase, args) -> int:
    target = int(args.id, 0)
    indices = db.record_indices_for_id_any_version(target)
    if not indices:
        print(f"id {target} not found (neither a canonical record id nor an alias id)")
        return 1
    if len(indices) > 1:
        print(f"NOTE: id {target} reaches {len(indices)} distinct records depending on "
              f"version scope (rare - printing all):")
    for idx in indices:
        rec = db.record_view(idx)
        via = "canonical id" if rec["id"] == target else f"alias id {target}"
        print(f"-- reached via {via} --")
        _print_record(db, rec)
    return 0


def cmd_rva(db: RuntimeDatabase, args) -> int:
    version = parse_version(args.version)
    target = int(args.hex, 16)
    records = db.rva_lookup(version, target)
    if not records:
        print(f"no record has a known RVA of 0x{target:X} at version {format_version(version)} "
              f"(family~{runtime_family_key(version)})")
        return 1
    for rec in records:
        _print_record(db, rec)
    return 0


def cmd_bridge(db: RuntimeDatabase, args) -> int:
    ae_id = int(args.aeid, 0)
    result = db.bridge(ae_id)
    if not result["records"] and result["legacy_og_table_offset"] is None:
        print(f"bridge: id {ae_id} is not present in the runtime database's records/aliases, "
              f"nor in the embedded legacy OG table.")
        return 1
    if result["legacy_og_table_offset"] is not None:
        print(f"embedded legacy OG table: id {ae_id} -> offset 0x{result['legacy_og_table_offset']:X} "
              f"(this is the hard-coded-OG-id fallback path IDDatabase::resolve() takes when the "
              f"automatic AE->runtime-database bridge fails)")
    for rec in result["records"]:
        via = "canonical id" if rec["id"] == ae_id else f"alias id {ae_id}"
        print(f"automatic bridge via runtime database ({via}):")
        _print_record(db, rec, indent="  ")
        other_ids = sorted({a["id"] for a in rec["aliases"]} | {rec["id"]} - {ae_id})
        if other_ids:
            print(f"  => {ae_id} bridges to: {other_ids}")
        else:
            print(f"  => no other ids on this record; {ae_id} does not bridge to anything else")
    return 0


def cmd_info(db: RuntimeDatabase, args) -> int:
    print(f"source: {db.source_path}")
    print(f"file size: {db.file_size:,} bytes")
    print(f"legacy OG table entries (before F4RDBIN payload): {len(db.legacy_ids)}")
    print(f"format: major=1 minor={db.format_minor}")
    print(f"payload CRC32 (header): 0x{db.payload_crc:08X}"
          + (" [verified]" if db.crc_verified else " [not verified - pass --verify-crc]"))
    print(f"record count: {db.record_count}")
    print(f"alias count: {db.alias_count}")
    print(f"known RVA count: {db.known_count}")
    print(f"candidate count: {db.candidate_count} (not decoded)")
    print(f"fragment count: {db.fragment_count} (not decoded)")
    print(f"constraint count: {db.constraint_count} (not decoded)")
    versions = db.distinct_versions()
    print(f"distinct versions present (from known RVAs): {len(versions)}")
    for v in versions:
        print(f"  {format_version(v):<14} family~{runtime_family_key(v)}")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--db", default=r"D:\GOGGames\Fallout 4 GOTY\Data\F4SE\Plugins\f4rd-runtime.bin",
        help="path to f4rd-runtime.bin")
    parser.add_argument("--no-cache", action="store_true", help="ignore/skip the pickle cache")
    parser.add_argument("--verify-crc", action="store_true",
                         help="verify the payload CRC32 (reads the whole payload; slow, only on first parse)")
    parser.add_argument("--progress", action="store_true", help="print parse progress to stderr")
    sub = parser.add_subparsers(dest="command", required=True)

    p_id = sub.add_parser("id", help="print a record's aliases and known RVAs by id")
    p_id.add_argument("id", help="decimal or 0x-hex id")
    p_id.set_defaults(func=cmd_id)

    p_rva = sub.add_parser("rva", help="find record(s) whose known RVA for a version equals a hex value")
    p_rva.add_argument("version", help='e.g. "1.10.163"')
    p_rva.add_argument("hex", help="hex RVA, with or without 0x prefix")
    p_rva.set_defaults(func=cmd_rva)

    p_bridge = sub.add_parser("bridge", help="show what the AE-id -> OG automatic bridge maps an id to")
    p_bridge.add_argument("aeid", help="decimal or 0x-hex AE id")
    p_bridge.set_defaults(func=cmd_bridge)

    p_info = sub.add_parser("info", help="database summary: counts + versions present")
    p_info.set_defaults(func=cmd_info)

    args = parser.parse_args(argv)
    db = load(args.db, use_cache=not args.no_cache, verify_crc=args.verify_crc, progress=args.progress)
    return args.func(db, args)


if __name__ == "__main__":
    raise SystemExit(main())
