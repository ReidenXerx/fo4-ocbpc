"""
Reader for Fallout 4 Address Library files (Data/F4SE/Plugins/version-<v>.bin).

Format (verified empirically against real version-1-10-163-0.bin /
version-1-10-984-0.bin / version-1-11-240-0.bin, and matching how
CommonLibF4RD's IDDatabase parses this exact table shape):

    struct File {
        uint64_t count;
        struct { uint64_t id; uint64_t offset; } entries[count];   // 16 bytes each
    };

- Little-endian, no compression, no delta/varint encoding of any kind.
- `entries` is sorted strictly ascending by `id` (id[i] < id[i+1] for all i).
  There ARE gaps in the id space (ids are not contiguous / not index-based) --
  a binary search on `id` is required, not `entries[id]`.
- `offset` is an RVA from the module's image base.
- This same layout (uint64 count + {id, offset} pairs, strictly ascending id,
  offset < image_size) is exactly what CommonLibF4RD's
  IDDatabase::load()::loadEmbeddedOGTable lambda in
  CommonLibF4RD/CommonLibF4/src/REL/Relocation.cpp validates and consumes for
  its embedded-OG-table path (it just reads it from a differently-named file,
  f4rd-runtime*.bin, guarded by a "F4RDBIN\\0" magic check at the front that
  version-<v>.bin files do NOT have). The original (non-RD) CommonLibF4's
  IDDatabase::load() in include/REL/Relocation.h reads `Data/F4SE/Plugins/
  version-{}.bin` with this identical struct, unconditionally:

      void load()
      {
          const auto version = Module::get().version();
          const auto path = fmt::format(
              "Data/F4SE/Plugins/version-{}.bin",
              version.string());
          if (!_mmap.open(path)) {
              stl::report_and_fail(fmt::format("failed to open: {}", path));
          }
          _id2offset = std::span{
              reinterpret_cast<const mapping_t*>(_mmap.data() + sizeof(std::uint64_t)),
              *reinterpret_cast<const std::uint64_t*>(_mmap.data())
          };
      }

  where `mapping_t` is `struct mapping_t { std::uint64_t id; std::uint64_t offset; };`.
  There is no OG-vs-NG/AE format difference and no decompression step anywhere
  in this path -- all three measured files (OG/NG/AE) parse with the exact
  same struct.

CLI:
    python addrlib.py <bin> id <n>      -> prints the offset (hex) for id n
    python addrlib.py <bin> off <hex>   -> prints the id(s) whose offset == hex value

Parsed tables are cached as pickles next to this script, under
tools/rd/.cache/, keyed by the source .bin's (size, mtime_ns).
"""

from __future__ import annotations

import pickle
import struct
import sys
from array import array
from pathlib import Path

_CACHE_DIR = Path(__file__).resolve().parent / ".cache"

_HEADER = struct.Struct("<Q")  # count


def _cache_path(bin_path: Path, key: tuple[int, int]) -> Path:
    size, mtime_ns = key
    return _CACHE_DIR / f"{bin_path.stem}.{size}.{mtime_ns}.pkl"


def load(path: str | Path) -> dict[int, int]:
    """Parse a Fallout 4 Address Library version-<v>.bin file.

    Returns a dict mapping id -> offset (RVA from the image base).
    Results are cached as a pickle in tools/rd/.cache/, keyed by the source
    file's (size, mtime_ns), so re-parsing a 25MB OG table is a one-time cost.
    """
    bin_path = Path(path).resolve()
    st = bin_path.stat()
    key = (st.st_size, st.st_mtime_ns)

    _CACHE_DIR.mkdir(parents=True, exist_ok=True)
    cache_file = _cache_path(bin_path, key)
    if cache_file.exists():
        with cache_file.open("rb") as f:
            return pickle.load(f)

    data = bin_path.read_bytes()
    if len(data) < _HEADER.size:
        raise ValueError(f"{bin_path}: file too small ({len(data)} bytes)")

    (count,) = _HEADER.unpack_from(data, 0)
    expected_size = _HEADER.size + count * 16
    if expected_size != len(data):
        raise ValueError(
            f"{bin_path}: size mismatch -- header claims {count} entries "
            f"({expected_size} bytes expected), file is {len(data)} bytes"
        )

    body = memoryview(data)[_HEADER.size :]
    values = array("Q")
    values.frombytes(body)
    if len(values) != 2 * count:
        raise ValueError(
            f"{bin_path}: expected {2 * count} u64 values in body, got {len(values)}"
        )

    ids = values[0::2]
    offsets = values[1::2]

    prev = -1
    for i in range(count):
        cur = ids[i]
        if cur <= prev:
            raise ValueError(
                f"{bin_path}: id list is not strictly ascending at index {i} "
                f"(id={cur}, prev={prev})"
            )
        prev = cur

    result = dict(zip(ids.tolist(), offsets.tolist()))

    # Stale/partial cache writes should never corrupt a concurrent reader.
    tmp_file = cache_file.with_suffix(".tmp")
    with tmp_file.open("wb") as f:
        pickle.dump(result, f, protocol=pickle.HIGHEST_PROTOCOL)
    tmp_file.replace(cache_file)

    return result


def _main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(
            "usage:\n"
            "  addrlib.py <bin> id <n>\n"
            "  addrlib.py <bin> off <hex>",
            file=sys.stderr,
        )
        return 2

    bin_path, mode, value = argv[1], argv[2], argv[3]
    table = load(bin_path)

    if mode == "id":
        target_id = int(value, 0)
        if target_id not in table:
            print(f"id {target_id} not found", file=sys.stderr)
            return 1
        print(hex(table[target_id]))
        return 0

    if mode == "off":
        target_off = int(value, 16)  # int(x, 16) also accepts an optional "0x" prefix
        matches = [i for i, o in table.items() if o == target_off]
        if not matches:
            print(f"offset {hex(target_off)} not found", file=sys.stderr)
            return 1
        for i in sorted(matches):
            print(i)
        return 0

    print(f"unknown mode: {mode!r} (expected 'id' or 'off')", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(_main(sys.argv))
