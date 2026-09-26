# SPDX-License-Identifier: GPL-3.0-only
"""Find a Fallout 4 1.10.163 (OG) function or call site in another runtime's executable (AE 1.11.x, NG 1.10.984),
for the Runtime Database port (CommonLibF4RD). Static analysis of the executables only; nothing is run.

    python tools/rd/sigfind.py fn   0x6689D0            # a function: its AE address and a signature
    python tools/rd/sigfind.py call 0x6860FA            # a call instruction: the same call in the AE function
    python tools/rd/sigfind.py fn   0x9C0410 --target "<exe>"   # another runtime's executable

How (each method says which one found it, and a match must be UNIQUE or it is reported as ambiguous, never
guessed):
  pattern   the OG function's bytes with every relocated field wildcarded (RIP-relative displacements, rel32
            call/jmp targets, rel8 branches), the longest prefix that still matches exactly once in the target's
            .text. Also printed as an IDA-style signature ("48 8B C4 ?? ...") for RD.
  strings   the unique .rdata strings the OG function references (lea/mov rip+disp): the target function that
            references the same string, confirmed by function size within 40%.
  calls     a call site: the OG call's ordinal among the calls to the same callee inside its function, then the
            same ordinal in the matched target function.
Function boundaries come from each executable's .pdata (exception directory).
"""
import argparse
import bisect
import re
import sys

import capstone
import pefile

OG = r'D:\GOGGames\Fallout 4 GOTY\Fallout4.exe'
AE = r'D:\SteamLibrary\steamapps\common\Fallout 4 AE\Fallout4.exe'


class Exe:
    def __init__(self, path):
        self.path = path
        self.pe = pefile.PE(path, fast_load=True)
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.sections = {s.Name.rstrip(b'\0').decode(): s for s in self.pe.sections}
        text = self.sections['.text']
        self.text_rva, self.text = text.VirtualAddress, text.get_data()[:text.Misc_VirtualSize]
        rd = self.sections['.rdata']
        self.rdata_rva, self.rdata = rd.VirtualAddress, rd.get_data()[:rd.Misc_VirtualSize]
        funcs = sorted({(e.struct.BeginAddress, e.struct.EndAddress) for e in self.pe.DIRECTORY_ENTRY_EXCEPTION})
        # chained unwind entries split one function into several ranges: keep the starts, merge by adjacency
        self.starts = [b for b, _ in funcs]
        self.ends = {b: e for b, e in funcs}
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def func_at(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0:
            return None
        b = self.starts[i]
        return (b, self.ends[b]) if rva < self.ends[b] else None

    def code(self, start, end):
        return self.text[start - self.text_rva:end - self.text_rva]

    def insns(self, start, end):
        return list(self.md.disasm(self.code(start, end), start))

    def string_at(self, rva):
        """A NUL-terminated ASCII string in .rdata at rva, or None."""
        if not (self.rdata_rva <= rva < self.rdata_rva + len(self.rdata)):
            return None
        o = rva - self.rdata_rva
        end = self.rdata.find(b'\0', o, o + 256)
        s = self.rdata[o:end] if end > o else b''
        return s.decode('ascii') if len(s) >= 6 and all(32 <= c < 127 for c in s) else None


def masked(exe, start, end):
    """(bytes, mask) of a function: mask False where a relocated field sits."""
    raw = bytearray(exe.code(start, end))
    mask = [True] * len(raw)
    for ins in exe.insns(start, end):
        off = ins.address - start
        imm_off = ins.imm_offset if hasattr(ins, 'imm_offset') else 0
        disp_off = ins.disp_offset if hasattr(ins, 'disp_offset') else 0
        rip = any(op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP for op in ins.operands)
        if rip and disp_off:
            for k in range(4):
                mask[off + disp_off + k] = False
        if ins.group(capstone.CS_GRP_CALL) or ins.group(capstone.CS_GRP_JUMP):
            if ins.size >= 5 and imm_off:
                for k in range(ins.size - imm_off):
                    mask[off + imm_off + k] = False
            elif ins.size == 2 and ins.bytes[0] in range(0x70, 0x80) or ins.bytes[0] == 0xEB:
                mask[off + 1] = False
    return bytes(raw), mask


def to_regex(raw, mask, n):
    return re.compile(b''.join(re.escape(raw[i:i + 1]) if mask[i] else b'.' for i in range(n)), re.S)


def signature(raw, mask, n):
    return ' '.join(f'{raw[i]:02X}' if mask[i] else '??' for i in range(n))


def by_pattern(og, target, start, end):
    raw, mask = masked(og, start, end)
    best = None
    for n in (16, 24, 32, 48, 64, 96, 128, 192, 256):
        if n > len(raw):
            n = len(raw)
        hits = [m.start() + target.text_rva for m in to_regex(raw, mask, n).finditer(target.text)
                if target.func_at(m.start() + target.text_rva) and
                target.func_at(m.start() + target.text_rva)[0] == m.start() + target.text_rva]
        if len(hits) == 1:
            return hits[0], signature(raw, mask, n), n
        if not hits:
            break
        best = (len(hits), n)
        if n == len(raw):
            break
    return None, best, None


def refs_strings(exe, start, end):
    out = []
    for ins in exe.insns(start, end):
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                s = exe.string_at(ins.address + ins.size + op.mem.disp)
                if s:
                    out.append(s)
    return out


def by_strings(og, target, start, end):
    size = end - start
    for s in refs_strings(og, start, end):
        needle = s.encode() + b'\0'
        where = [m.start() for m in re.finditer(re.escape(needle), target.rdata)]
        if len(where) != 1 or target.rdata[where[0] - 1:where[0]] not in (b'\0', b''):
            continue
        srva = target.rdata_rva + where[0]
        users = set()
        # every rip-relative reference to srva: scan for the 4-byte displacement candidates is too slow; walk
        # lea/mov instructions of the form 48 8D ?? disp32 / 4C 8D ?? disp32
        for m in re.finditer(rb'[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]', target.text):
            at = m.start() + target.text_rva
            disp = int.from_bytes(target.text[m.start() + 3:m.start() + 7], 'little', signed=True)
            if at + 7 + disp == srva:
                f = target.func_at(at)
                if f:
                    users.add(f)
        fits = [f for f in users if 0.6 * size <= f[1] - f[0] <= 1.4 * size]
        if len(fits) == 1:
            return fits[0][0], s
    return None, None


def calls_in(exe, start, end):
    out = []
    for ins in exe.insns(start, end):
        if ins.group(capstone.CS_GRP_CALL) and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
            out.append((ins.address, ins.operands[0].imm))
    return out


def find_function(og, target, rva):
    f = og.func_at(rva)
    if not f:
        sys.exit(f'OG {rva:#x}: not inside any .pdata function')
    start, end = f
    hit, sig, n = by_pattern(og, target, start, end)
    if hit:
        return start, end, hit, f'pattern ({n} bytes, unique)', sig
    ambiguous = sig
    hit, s = by_strings(og, target, start, end)
    if hit:
        return start, end, hit, f'strings ("{s[:50]}", size within 40%)', None
    why = f'pattern ambiguous ({ambiguous[0]} hits at {ambiguous[1]} bytes)' if ambiguous else 'pattern: no hit'
    return start, end, None, why + '; strings: no unique referenced string', None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('kind', choices=('fn', 'call'))
    ap.add_argument('rva', help='OG 1.10.163 address as an offset from the image base (hex)')
    ap.add_argument('--og', default=OG)
    ap.add_argument('--target', default=AE)
    a = ap.parse_args()
    og, target = Exe(a.og), Exe(a.target)
    rva = int(a.rva, 16)
    start, end, hit, how, sig = find_function(og, target, rva)
    print(f'OG function {start:#x}..{end:#x} ({end - start} bytes)' + (f', {rva - start:#x} into it' if rva != start else ''))
    if not hit:
        print(f'NOT FOUND in {target.path}: {how}')
        return 1
    tf = target.func_at(hit)
    print(f'target function {hit:#x}..{tf[1]:#x} ({tf[1] - tf[0]} bytes)  by {how}')
    if sig:
        print(f'signature: {sig}')
    if a.kind == 'call':
        og_calls = calls_in(og, start, end)
        mine = [c for c in og_calls if c[0] == rva]
        if not mine:
            print(f'OG {rva:#x} is not a direct call instruction')
            return 1
        callee = mine[0][1]
        k = [c for c in og_calls if c[1] == callee].index(mine[0])
        cstart, cend, chit, chow, _ = find_function(og, target, callee)
        if not chit:
            print(f'callee OG {callee:#x}: NOT FOUND ({chow})')
            return 1
        same = [c for c in calls_in(target, tf[0], tf[1]) if c[1] == chit]
        print(f'callee OG {callee:#x} -> target {chit:#x} by {chow}; OG call is #{k} of {len([c for c in og_calls if c[1] == callee])} '
              f'to it, target has {len(same)}')
        if len(same) == len([c for c in og_calls if c[1] == callee]):
            print(f'target call site: {same[k][0]:#x}  (offset {same[k][0] - tf[0]:#x} into the function)')
        else:
            print('call counts differ: the site is NOT resolved (check by hand)')
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
