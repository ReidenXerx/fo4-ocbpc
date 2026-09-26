# SPDX-License-Identifier: GPL-3.0-only
"""Mechanical first pass of a classic engine source file (CBPSSE/, the F4SE 0.6.23 SDK) into the Runtime Database
build (CBPSSE-RD/, CommonLibF4RD through CBPSSE-RD/Game.h). Only rewrites that mean the same thing on both sides;
everything else is left for the compiler to point at and a person to port.

    python tools/rd/port.py Bones.cpp Bones.h ...     # CBPSSE/<f> -> CBPSSE-RD/<f> (refuses to overwrite)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC, DST = ROOT / 'CBPSSE', ROOT / 'CBPSSE-RD'
OPERAND = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.>-:')


def operand_start(s, end):
    """The start of the postfix expression that ends at `end` (exclusive): identifiers, ., ->, ::, [..], (..)."""
    i = end
    while i > 0:
        c = s[i - 1]
        if c in ')]':
            close, open_ = c, '(' if c == ')' else '['
            depth, j = 0, i - 1
            while j >= 0:
                if s[j] == close:
                    depth += 1
                elif s[j] == open_:
                    depth -= 1
                    if depth == 0:
                        break
                j -= 1
            i = j
            continue
        if c == '>' and i >= 2 and s[i - 2] == '-':
            i -= 2
            continue
        if c in OPERAND and c not in '>-':
            i -= 1
            continue
        break
    return i


def wrap_member(s, member, func, tail=''):
    """X->member -> func(X)tail, innermost first so chains nest."""
    pat = re.compile(r'->' + re.escape(member) + r'\b')
    while True:
        m = pat.search(s)
        if not m:
            return s
        start = operand_start(s, m.start())
        operand = s[start:m.start()]
        s = s[:start] + f'{func}({operand}){tail}' + s[m.end():]


def wrap_call(s, method, func):
    """X->method() -> func(X)"""
    pat = re.compile(r'->' + re.escape(method) + r'\(\)')
    while True:
        m = pat.search(s)
        if not m:
            return s
        start = operand_start(s, m.start())
        s = s[:start] + f'{func}({s[start:m.start()]})' + s[m.end():]


def port(text):
    out = []
    for line in text.splitlines(keepends=True):
        if re.match(r'\s*#include\s+"f4se(_common)?[/\\]', line):
            continue
        out.append(line)
    s = ''.join(out)
    s = re.sub(r'(#include\s+"[^"]+"\s*\n)', r'\1#include "Game.h"\n', s, count=1) if '#include "Game.h"' not in s else s
    # actor/reference 3D
    s = wrap_member(s, 'unkF0->rootNode', 'G::Root')
    s = wrap_member(s, 'unkF0', 'G::Root')
    # nodes: the children array first (it carries the node expression), then the transform, parent, name
    s = re.sub(r'->m_children\.m_emptyRunStart', '->__CHILDCOUNT__', s)
    s = wrap_member(s, '__CHILDCOUNT__', 'G::ChildCount')
    s = re.sub(r'->m_children\.m_data\[', '->__CHILD__[', s)
    while '->__CHILD__[' in s:
        i = s.index('->__CHILD__[')
        start = operand_start(s, i)
        j, depth = i + len('->__CHILD__['), 1
        while depth:
            depth += {'[': 1, ']': -1}.get(s[j], 0)
            j += 1
        s = s[:start] + f'G::Child({s[start:i]}, {s[i + len("->__CHILD__["):j - 1]})' + s[j:]
    s = wrap_member(s, 'm_parent', 'G::Parent')
    s = wrap_member(s, 'm_localTransform', 'G::Local')
    s = wrap_member(s, 'm_worldTransform', 'G::World')
    s = re.sub(r'->m_name\.c_str\(\)', '->__NAME__', s)
    s = wrap_member(s, '__NAME__', 'G::Name')
    s = wrap_call(s, 'GetAsNiNode', 'G::AsNode')
    s = wrap_call(s, 'GetAsBSGeometry', 'G::AsGeometry')
    s = s.replace('GetObjectByName(&', 'GetObjectByName(')
    s = re.sub(r'DYNAMIC_CAST\(\s*LookupFormByID\(([^()]*)\)\s*,\s*TESForm\s*,\s*Actor\s*\)', r'G::LookupActor(\1)', s)
    s = re.sub(r'\(\s*(\w+)->flags\s*&\s*TESForm::kFlag_IsDeleted\s*\)', r'G::Deleted(\1)', s)
    s = re.sub(r'(\w+)->flags\s*&\s*TESForm::kFlag_IsDeleted', r'G::Deleted(\1)', s)
    return s


def main():
    for name in sys.argv[1:]:
        src, dst = SRC / name, DST / name
        if dst.exists():
            print(f'{dst.name}: exists, not overwritten')
            continue
        text = src.read_bytes().decode('utf-8')
        dst.write_bytes(port(text).replace('\r\n', '\n').encode('utf-8'))
        print(f'{name} -> CBPSSE-RD/{name}')


if __name__ == '__main__':
    main()
