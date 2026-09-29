---
name: gitnexus-area-rd
description: "Skill for the Rd area of OpenCBP_FO4. 56 symbols across 11 files."
---

# Rd

56 symbols | 11 files | Cohesion: 73%

## When to Use

- Working with code in `tools/`
- Understanding how main, callsite, main work
- Modifying rd-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `tools/rd/f4rd_db.py` | _print_record, cmd_info, cmd_rva, format_version, runtime_family_key (+14) |
| `tools/rd/xmatch.py` | _interp, _landmarks, callsite, main, match (+7) |
| `tools/rd/port.py` | main, operand_start, port, wrap_call, wrap_member |
| `tools/rd/fnroot.py` | Functions, main, logical, root |
| `tools/rd/sigfind.py` | by_pattern, by_strings, find_function, main |
| `tools/rd/dbanchors.py` | main, db_pairs, load |
| `tools/rd/disp.py` | displacements, main |
| `tools/rd/bytecallers.py` | main, scan |
| `tools/rd/addrlib.py` | _main, load |
| `tools/rd/opscan.py` | load, main |

## Entry Points

Start here when exploring this area:

- **`main`** (Function) — `tools/rd/dbanchors.py:64`
- **`callsite`** (Function) — `tools/rd/xmatch.py:409`
- **`main`** (Function) — `tools/rd/xmatch.py:522`
- **`match`** (Function) — `tools/rd/xmatch.py:338`
- **`selftest`** (Function) — `tools/rd/xmatch.py:464`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Functions` | Class | `tools/rd/fnroot.py` | 22 |
| `main` | Function | `tools/rd/dbanchors.py` | 64 |
| `callsite` | Function | `tools/rd/xmatch.py` | 409 |
| `main` | Function | `tools/rd/xmatch.py` | 522 |
| `match` | Function | `tools/rd/xmatch.py` | 338 |
| `selftest` | Function | `tools/rd/xmatch.py` | 464 |
| `cmd_info` | Function | `tools/rd/f4rd_db.py` | 616 |
| `cmd_rva` | Function | `tools/rd/f4rd_db.py` | 580 |
| `format_version` | Function | `tools/rd/f4rd_db.py` | 100 |
| `runtime_family_key` | Function | `tools/rd/f4rd_db.py` | 71 |
| `main` | Function | `tools/rd/port.py` | 104 |
| `operand_start` | Function | `tools/rd/port.py` | 16 |
| `port` | Function | `tools/rd/port.py` | 67 |
| `wrap_call` | Function | `tools/rd/port.py` | 56 |
| `wrap_member` | Function | `tools/rd/port.py` | 44 |
| `displacements` | Function | `tools/rd/disp.py` | 21 |
| `main` | Function | `tools/rd/disp.py` | 41 |
| `main` | Function | `tools/rd/vslot.py` | 40 |
| `main` | Function | `tools/rd/bytecallers.py` | 39 |
| `scan` | Function | `tools/rd/bytecallers.py` | 16 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Main → Func_index` | cross_community | 5 |
| `Main → _cache_path` | cross_community | 5 |
| `Main → Func_index` | cross_community | 5 |
| `Main → _interp` | intra_community | 4 |
| `Main → _landmarks` | intra_community | 4 |
| `Main → _interp` | intra_community | 4 |
| `Main → _landmarks` | intra_community | 4 |
| `Cmd_id → Format_version` | cross_community | 3 |
| `Cmd_id → Runtime_family_key` | cross_community | 3 |
| `Cmd_id → Record_index_by_canonical_id` | cross_community | 3 |

## How to Explore

1. `context({name: "main"})` — see callers and callees
2. `query({search_query: "rd"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
