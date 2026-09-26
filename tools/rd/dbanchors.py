# SPDX-License-Identifier: GPL-3.0-only
"""xmatch.py with Runtime Database's own OG<->AE pairs as anchors: every f4rd-runtime.bin record that knows its
1.10.163 address (a known RVA, or an OG alias through the OG Address Library) and its AE address (a known 1.11.240 RVA,
or its AE id through the AE Address Library). RD matched these itself; the ~580 function pairs in CommonLibF4RD's
headers were too few to reach our functions (xmatch selftest 2026-09-26: 18.5% top-1, 12/200 wrong-but-confident).

    python tools/rd/dbanchors.py selftest [N]
    python tools/rd/dbanchors.py fn <og_hex> ...
    python tools/rd/dbanchors.py site <og_hex> ...
"""
import pickle
import sys

import addrlib
import f4rd_db
import xmatch

DB = r'D:\GOGGames\Fallout 4 GOTY\Data\F4SE\Plugins\f4rd-runtime.bin'
OG_V, AE_V = (1, 10, 163, 0), (1, 11, 240, 0)


def db_pairs():
    cache = xmatch.CACHE_DIR / 'dbanchors-pairs.pkl'
    if cache.exists():
        return pickle.loads(cache.read_bytes())
    db = f4rd_db.load(DB)
    og_tab, ae_tab = addrlib.load(xmatch.OG_BIN), addrlib.load(xmatch.AE_BIN)
    pairs = []
    for rec in db.records():
        known = {tuple(k['version']): k['rva'] for k in rec['known_rvas']}
        og = known.get(OG_V)
        if og is None:
            for al in rec['aliases']:
                if tuple(al['version'])[:2] == (1, 10) and tuple(al['version']) < (1, 10, 980, 0):
                    og = og_tab.get(al['id'])
                    if og is not None:
                        break
        ae = known.get(AE_V, ae_tab.get(rec['id']))
        if og is not None and ae is not None:
            pairs.append((og, ae))
    cache.write_bytes(pickle.dumps(pairs))
    return pairs


def load():
    og, ae, amap, stats = xmatch._load()
    added = conflicts = 0
    for og_off, ae_off in db_pairs():
        if not (og.text_rva <= og_off < og.text_end and ae.text_rva <= ae_off < ae.text_end):
            continue
        a, b = og.func_start(og_off), ae.func_start(ae_off)
        if a is None or b is None:
            continue
        if a in amap and amap[a] != b:
            conflicts += 1
            continue
        if a not in amap:
            added += 1
        amap[a] = b
    print(f'[anchors] headers: {len(amap) - added}, from f4rd-runtime.bin: +{added} ({conflicts} conflicting, dropped); '
          f'total {len(amap)}')
    return og, ae, amap


def main(argv):
    og, ae, amap = load()
    if argv[0] == 'selftest':
        xmatch.selftest(og, ae, amap, n=int(argv[1]) if len(argv) > 1 else 200)
    elif argv[0] == 'fn':
        for a in argv[1:]:
            print(f'--- fn {a}')
            print(xmatch.match(og, ae, amap, int(a, 16)))
    elif argv[0] == 'site':
        for a in argv[1:]:
            print(f'--- site {a}')
            print(xmatch.callsite(og, ae, amap, int(a, 16)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
