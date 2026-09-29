---
name: gitnexus-area-msvc
description: "Skill for the Msvc area of OpenCBP_FO4. 15 symbols across 2 files."
---

# Msvc

15 symbols | 2 files | Cohesion: 89%

## When to Use

- Working with code in `extern/`
- Understanding how operator<=>, operator==, unique_ptr work
- Modifying msvc-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | operator<=>, operator==, get, good, operator* (+8) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/functional.h` | good, operator() |

## Entry Points

Start here when exploring this area:

- **`operator<=>`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h:698`
- **`operator==`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h:691`
- **`unique_ptr`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h:8`
- **`get`** (Method) — `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h:393`
- **`good`** (Method) — `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h:413`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `unique_ptr` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 8 |
| `operator<=>` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 698 |
| `operator==` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 691 |
| `get` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 393 |
| `good` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 413 |
| `operator*` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 399 |
| `operator->` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 406 |
| `operator=` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 331 |
| `release` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 368 |
| `reset` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 375 |
| `get` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 629 |
| `good` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 642 |
| `operator[]` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/memory.h` | 635 |
| `good` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/functional.h` | 39 |
| `operator()` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/msvc/functional.h` | 18 |

## How to Explore

1. `context({name: "operator<=>"})` — see callers and callees
2. `query({search_query: "msvc"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
