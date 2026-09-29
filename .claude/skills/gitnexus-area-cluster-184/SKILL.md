---
name: gitnexus-area-cluster-184
description: "Skill for the Cluster_184 area of OpenCBP_FO4. 6 symbols across 2 files."
---

# Cluster_184

6 symbols | 2 files | Cohesion: 92%

## When to Use

- Working with code in `common/`
- Understanding how Test_IMemPool, Allocate, Clear work
- Modifying cluster_184-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `common/IMemPool.h` | Allocate, Clear, Dump, Free, GetObj |
| `common/IMemPool.cpp` | Test_IMemPool |

## Entry Points

Start here when exploring this area:

- **`Test_IMemPool`** (Function) — `common/IMemPool.cpp:2`
- **`Allocate`** (Method) — `common/IMemPool.h:28`
- **`Clear`** (Method) — `common/IMemPool.h:122`
- **`Dump`** (Method) — `common/IMemPool.h:93`
- **`Free`** (Method) — `common/IMemPool.h:47`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Test_IMemPool` | Function | `common/IMemPool.cpp` | 2 |
| `Allocate` | Method | `common/IMemPool.h` | 28 |
| `Clear` | Method | `common/IMemPool.h` | 122 |
| `Dump` | Method | `common/IMemPool.h` | 93 |
| `Free` | Method | `common/IMemPool.h` | 47 |
| `GetObj` | Method | `common/IMemPool.h` | 134 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Test_IMemPool → GetObj` | intra_community | 3 |

## How to Explore

1. `context({name: "Test_IMemPool"})` — see callers and callees
2. `query({search_query: "cluster_184"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
