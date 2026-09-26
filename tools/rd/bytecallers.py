# SPDX-License-Identifier: GPL-3.0-only
"""Every copy of an exact byte sequence in an executable's .text, with each copy's direct callers (by logical
function root). For a tiny leaf that compiles the same in every runtime, its callers then name the function around it.

    python tools/rd/bytecallers.py 33C048894 10C48894104C3            # OG and AE
"""
import pathlib
import re
import sys

import pefile

import fnroot
import xmatch


def scan(path, pattern, show=12):
    fx = fnroot.Functions(path)
    pe = pefile.PE(path, fast_load=True)
    t = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    base = t.VirtualAddress
    data = bytes(fx.image[base:base + t.Misc_VirtualSize])
    hits = [base + m.start() for m in re.finditer(re.escape(pattern), data)]
    targets = set(hits)
    callers = {h: [] for h in hits}
    for m in re.finditer(rb'\xE8', data):
        i = m.start()
        if i + 5 > len(data):
            break
        tgt = base + i + 5 + int.from_bytes(data[i + 1:i + 5], 'little', signed=True)
        if tgt in targets:
            callers[tgt].append(base + i)
    print(f'{pathlib.Path(path).parent.name}: {len(hits)} copies')
    for h in hits[:show]:
        roots = sorted({fx.logical(c)[0] for c in callers[h] if fx.logical(c)})
        print(f'   {h:#x}: {len(callers[h])} direct call(s): ' + ', '.join(f'{c:#x}' for c in callers[h][:6])
              + f'  roots {[hex(r) for r in roots][:6]}')


def main():
    pattern = bytes.fromhex(''.join(sys.argv[1:]))
    scan(fnroot.OG, pattern)
    scan(xmatch.AE_EXE, pattern)
    return 0


if __name__ == '__main__':
    sys.exit(main())
