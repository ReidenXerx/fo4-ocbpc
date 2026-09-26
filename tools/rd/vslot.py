# SPDX-License-Identifier: GPL-3.0-only
"""A vtable's slots in OG and AE side by side, and the member displacements each slot's function uses: how a class's
layout is compared between runtimes from the code that reads it.

    python tools/rd/vslot.py 0x2E14818 0x26849B8 0x3A 0x2E        # NiNode: AttachChild, GetObjectByName
"""
import struct
import sys

import capstone
import pefile

import fnroot
import xmatch

BASE = 0x140000000


def slot_fn(fx, vtable_rva, slot):
    return struct.unpack_from('<Q', fx.image, vtable_rva + slot * 8)[0] - BASE


def disps(fx, rva):
    got = fx.logical(rva)
    if not got:
        return rva, []
    root, chunks = got
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    out = []
    for b, e in chunks:
        for ins in md.disasm(bytes(fx.image[b:e]), b):
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base not in (0, capstone.x86.X86_REG_RIP,
                                                                                capstone.x86.X86_REG_RSP):
                    if 0x20 <= op.mem.disp < 0x400:
                        out.append((op.mem.disp, f'{ins.address:#x} {ins.mnemonic} {ins.op_str}'))
    return root, out


def main():
    og_vt, ae_vt = int(sys.argv[1], 16), int(sys.argv[2], 16)
    og, ae = fnroot.Functions(fnroot.OG), fnroot.Functions(xmatch.AE_EXE)
    for s in sys.argv[3:]:
        slot = int(s, 16)
        o, a = slot_fn(og, og_vt, slot), slot_fn(ae, ae_vt, slot)
        _, od = disps(og, o)
        _, ad = disps(ae, a)
        print(f'slot {slot:#x}: OG {o:#x}  AE {a:#x}')
        print('   OG displacements:', sorted({d for d, _ in od}))
        print('   AE displacements:', sorted({d for d, _ in ad}))
        for d, line in od[:10]:
            print('      OG', line)
        for d, line in ad[:10]:
            print('      AE', line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
