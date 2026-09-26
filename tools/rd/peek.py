# SPDX-License-Identifier: GPL-3.0-only
"""READ-ONLY look at the running Fallout4.exe: the bytes at a call site (an RVA of the exe), where its rel32 lands, and
which loaded module (or unowned trampoline memory) that target lies in. Nothing is written; the process is opened
with PROCESS_QUERY_INFORMATION | PROCESS_VM_READ only.

    python tools/rd/peek.py 0x1A815BC [more RVAs...]
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys

PROCESS_QUERY_INFORMATION, PROCESS_VM_READ = 0x0400, 0x0010
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)


class MODULEINFO(ctypes.Structure):
    _fields_ = [('lpBaseOfDll', ctypes.c_void_p), ('SizeOfImage', wt.DWORD), ('EntryPoint', ctypes.c_void_p)]


def pid_of(name='Fallout4.exe'):
    out = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {name}', '/FO', 'CSV', '/NH'], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0].lower() == name.lower():
            return int(parts[1])
    return None


def modules(h):
    arr = (ctypes.c_void_p * 1024)()
    needed = wt.DWORD()
    psapi.EnumProcessModulesEx(h, arr, ctypes.sizeof(arr), ctypes.byref(needed), 3)
    out = []
    for i in range(needed.value // ctypes.sizeof(ctypes.c_void_p)):
        name = ctypes.create_unicode_buffer(260)
        psapi.GetModuleBaseNameW(h, ctypes.c_void_p(arr[i]), name, 260)
        info = MODULEINFO()
        psapi.GetModuleInformation(h, ctypes.c_void_p(arr[i]), ctypes.byref(info), ctypes.sizeof(info))
        out.append((info.lpBaseOfDll, info.SizeOfImage, name.value))
    return out


def read(h, addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
        return None
    return buf.raw[:got.value]


def owner(mods, addr):
    for base, size, name in mods:
        if base <= addr < base + size:
            return f'{name}+{addr - base:#x}'
    return 'no module (a trampoline or allocated memory)'


def main():
    pid = pid_of()
    if not pid:
        sys.exit('Fallout4.exe is not running')
    h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    mods = modules(h)
    exe = next(m for m in mods if m[2].lower() == 'fallout4.exe')
    print(f'Fallout4.exe pid {pid} base {exe[0]:#x}')
    for s in sys.argv[1:]:
        site = exe[0] + int(s, 16)
        b = read(h, site, 5)
        if not b:
            print(f'{s}: unreadable')
            continue
        line = f'{s}: {b.hex(" ")}'
        if b[0] in (0xE8, 0xE9):
            tgt = site + 5 + int.from_bytes(b[1:5], 'little', signed=True)
            line += f' -> {tgt:#x} = {owner(mods, tgt)}'
            stub = read(h, tgt, 14)
            if stub and stub[:2] == b'\xff\x25':
                final = int.from_bytes(stub[6:14], 'little')
                line += f'; its jmp [rip] goes to {final:#x} = {owner(mods, final)}'
        print(line)
    k32.CloseHandle(h)
    return 0


if __name__ == '__main__':
    sys.exit(main())
