# SPDX-License-Identifier: GPL-3.0-only
"""The logical function an address belongs to, the way CommonLibF4RD computes it for REL::AUTO_CALLSITE
(src/REL/Relocation.cpp, logical_function_scopes): the .pdata entry holding the address, followed through chained
unwind info (UNW_FLAG_CHAININFO) to its root, then every chunk whose root is the same. The ROOT's start is the owner
an Address Library id must name.

    python tools/rd/fnroot.py 0x6860FA 0xD38B13 ...            # OG by default
    python tools/rd/fnroot.py --exe "<exe>" --bin "<version bin>" 0x...
"""
import argparse
import bisect
import struct
import sys

import pefile

import addrlib

OG = r'D:\GOGGames\Fallout 4 GOTY\Fallout4.exe'
OG_BIN = r'D:\GOGGames\Fallout 4 GOTY\Data\F4SE\Plugins\version-1-10-163-0.bin'


class Functions:
    def __init__(self, path):
        pe = pefile.PE(path, fast_load=True)
        self.image = pe.get_memory_mapped_image()
        d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']]
        raw = self.image[d.VirtualAddress:d.VirtualAddress + d.Size]
        self.rf = [struct.unpack_from('<III', raw, o) for o in range(0, len(raw) - 11, 12)]   # begin, end, unwind
        self.begins = [b for b, _, _ in self.rf]
        self._root = {}

    def root(self, entry):
        """RD's rootOf: follow UNW_FLAG_CHAININFO (flags bit 0x4 of the header's top 5 bits) to the root entry."""
        seen = set()
        b, e, u = entry
        while u:
            if u in seen:
                return None
            seen.add(u)
            header = self.image[u]
            if ((header >> 3) & 0x4) == 0:
                break
            codes = self.image[u + 2]
            chained = u + 4 + ((codes + 1) & ~1) * 2
            b, e, u = struct.unpack_from('<III', self.image, chained)
        return (b, e)

    def entry_at(self, rva):
        i = bisect.bisect_right(self.begins, rva) - 1
        while i >= 0:
            b, e, u = self.rf[i]
            if b <= rva < e:
                return self.rf[i]
            if e <= rva and rva - b > 0x100000:
                break
            i -= 1
        return None

    def logical(self, rva):
        """(root_begin, [chunks]) for the address, or None."""
        entry = self.entry_at(rva)
        if not entry:
            return None
        root = self.root(entry)
        if not root:
            return None
        chunks = sorted((b, e) for b, e, u in self.rf if self.root((b, e, u)) == root)
        return root[0], chunks


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('rvas', nargs='+')
    ap.add_argument('--exe', default=OG)
    ap.add_argument('--bin', default=OG_BIN)
    a = ap.parse_args()
    fx = Functions(a.exe)
    inv = {}
    for k, v in addrlib.load(a.bin).items():
        inv.setdefault(v, k)
    for s in a.rvas:
        rva = int(s, 16)
        got = fx.logical(rva)
        if not got:
            print(f'{rva:#x}: no .pdata function')
            continue
        root, chunks = got
        print(f'{rva:#x}: root {root:#x} (Address Library id {inv.get(root)}), {len(chunks)} chunk(s): '
              + ', '.join(f'{b:#x}..{e:#x}' for b, e in chunks[:6]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
