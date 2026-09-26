# SPDX-License-Identifier: GPL-3.0-only
"""The memory displacements a function uses ([reg+disp]), over its whole logical function (fnroot.py), with how often:
the way to see whether a class member a hook reads moved between runtimes. Run it on the 1.10.163 function and on its
twin in another runtime (xmatch.py) and compare the offsets that matter.

    python tools/rd/disp.py 0x6689D0                        # OG
    python tools/rd/disp.py 0x<ae> --exe "<AE exe>"
    python tools/rd/disp.py 0x6689D0 --only 0x18,0xF0,0x1C8,0x2C0
"""
import argparse
import collections
import sys

import capstone
import pefile

import fnroot

OG = fnroot.OG


def displacements(exe_path, rva):
    fx = fnroot.Functions(exe_path)
    got = fx.logical(rva)
    if not got:
        sys.exit(f'{rva:#x}: no .pdata function')
    root, chunks = got
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    uses = collections.defaultdict(list)
    for b, e in chunks:
        code = bytes(fx.image[b:e])
        for ins in md.disasm(code, b):
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base not in (0, capstone.x86.X86_REG_RIP,
                                                                                capstone.x86.X86_REG_RSP,
                                                                                capstone.x86.X86_REG_RBP):
                    uses[op.mem.disp].append(f'{ins.address:#x} {ins.mnemonic} {ins.op_str}')
    return root, chunks, uses


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('rva')
    ap.add_argument('--exe', default=OG)
    ap.add_argument('--only', help='comma-separated hex displacements to show with their instructions')
    a = ap.parse_args()
    root, chunks, uses = displacements(a.exe, int(a.rva, 16))
    print(f'function {root:#x}: {len(chunks)} chunk(s), {sum(e - b for b, e in chunks)} bytes')
    if a.only:
        for d in (int(x, 16) for x in a.only.split(',')):
            print(f'  +{d:#x}: {len(uses.get(d, []))} use(s)')
            for line in uses.get(d, [])[:6]:
                print('      ' + line)
    else:
        for d in sorted(uses):
            if 0 <= d < 0x1000:
                print(f'  +{d:#x}: {len(uses[d])}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
