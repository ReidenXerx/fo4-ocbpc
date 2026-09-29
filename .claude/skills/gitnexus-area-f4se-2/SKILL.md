---
name: gitnexus-area-f4se-2
description: "Skill for the F4SE area of OpenCBP_FO4. 19 symbols across 3 files."
---

# F4SE

19 symbols | 3 files | Cohesion: 85%

## When to Use

- Working with code in `extern/`
- Understanding how AllocTrampoline, GetTrampolineInterface, Init work
- Modifying f4se-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | allocate, do_allocate, in_range, write_5branch, write_6branch (+5) |
| `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | EditorVersion, F4SEVersion, GetProxy, MakeVersion, RuntimeVersion |
| `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp` | AllocTrampoline, GetTrampolineInterface, Init, get |

## Entry Points

Start here when exploring this area:

- **`AllocTrampoline`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp:158`
- **`GetTrampolineInterface`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp:147`
- **`Init`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp:54`
- **`EditorVersion`** (Method) — `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h:123`
- **`F4SEVersion`** (Method) — `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h:124`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `AllocTrampoline` | Function | `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp` | 158 |
| `GetTrampolineInterface` | Function | `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp` | 147 |
| `Init` | Function | `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp` | 54 |
| `EditorVersion` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 123 |
| `F4SEVersion` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 124 |
| `GetProxy` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 106 |
| `RuntimeVersion` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 128 |
| `allocate` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 108 |
| `do_allocate` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 191 |
| `write_5branch` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 203 |
| `write_6branch` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 259 |
| `allocate` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 116 |
| `create` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 66 |
| `log_stats` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 335 |
| `release` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 337 |
| `set_trampoline` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 90 |
| `MakeVersion` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h` | 112 |
| `in_range` | Method | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/Trampoline.h` | 183 |
| `get` | Method | `extern/CommonLibF4RD/CommonLibF4/src/F4SE/API.cpp` | 19 |

## How to Explore

1. `context({name: "AllocTrampoline"})` — see callers and callees
2. `query({search_query: "f4se"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
