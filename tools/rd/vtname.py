# SPDX-License-Identifier: GPL-3.0-only
"""The C++ class a vtable belongs to, read from the executable's own RTTI: vtable[-1] is the Complete Object Locator,
whose type descriptor carries the decorated name (.?AVClass@@).

    python tools/rd/vtname.py ae 0x2913528 0x29135C0
    python tools/rd/vtname.py og 0x...
"""
import struct
import sys

import pefile

import fnroot
import xmatch

EXES = {'og': fnroot.OG, 'ae': xmatch.AE_EXE}


def name_of(img, base, vt_rva):
    col_va = struct.unpack_from('<Q', img, vt_rva - 8)[0]
    col = col_va - base
    td_rva = struct.unpack_from('<I', img, col + 12)[0]
    end = img.index(b'\0', td_rva + 16)
    return img[td_rva + 16:end].decode('ascii', 'replace')


def main():
    pe = pefile.PE(EXES[sys.argv[1]], fast_load=True)
    img = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    for s in sys.argv[2:]:
        rva = int(s, 16)
        try:
            print(f'{rva:#x}: {name_of(img, base, rva)}')
        except Exception as e:
            print(f'{rva:#x}: not a vtable with RTTI ({e})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
