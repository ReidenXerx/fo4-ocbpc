---
name: gitnexus-area-rel
description: "Skill for the REL area of OpenCBP_FO4. 138 symbols across 3 files."
---

# REL

138 symbols | 3 files | Cohesion: 66%

## When to Use

- Working with code in `extern/`
- Understanding how commonReadable, findVTable, functionContaining work
- Modifying rel-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | commonReadable, findVTable, functionContaining, inSegment, inSegment (+81) |
| `extern/CommonLibF4RD/CommonLibF4/include/REL/Relocation.h` | segment, address, offset, size, base (+30) |
| `extern/CommonLibF4RD/CommonLibF4/src/REL/Relocation.cpp` | rootOf, logical_function_scopes, module_readable, resolve_callsites, id2offset (+12) |

## Entry Points

Start here when exploring this area:

- **`commonReadable`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:2536`
- **`findVTable`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:3413`
- **`functionContaining`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:3384`
- **`inSegment`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:2526`
- **`inSegment`** (Function) — `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp:2839`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `commonReadable` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2536 |
| `findVTable` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3413 |
| `functionContaining` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3384 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2526 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2839 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3218 |
| `resolveChain` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3574 |
| `lock` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2111 |
| `stateLock` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 4367 |
| `inResultSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2206 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2201 |
| `pointerRVA` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2219 |
| `readPointer` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2230 |
| `readU32` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2239 |
| `readable` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2209 |
| `validAscii` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2248 |
| `validHierarchy` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2285 |
| `validLocator` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2310 |
| `validTypeDescriptor` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 2264 |
| `inSegment` | Function | `extern/CommonLibF4RD/CommonLibF4/src/REL/RuntimeDatabase.cpp` | 3249 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Id2offset → Has_ng_id` | cross_community | 6 |
| `Id2offset → Has_og_id` | cross_community | 6 |
| `Lock → Read_le` | intra_community | 5 |
| `ValidMsvcBaseDescriptor → Address` | cross_community | 5 |
| `Id2offset → Runtime_family` | cross_community | 5 |
| `Id2offset → Ae_id` | cross_community | 5 |
| `Known_mappings → Runtime_family_key` | cross_community | 5 |
| `ValidMsvcHierarchy → Address` | cross_community | 5 |
| `ValidMsvcVtable → Address` | cross_community | 5 |
| `ValidHierarchy → Address` | cross_community | 5 |

## How to Explore

1. `context({name: "commonReadable"})` — see callers and callees
2. `query({search_query: "rel"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
