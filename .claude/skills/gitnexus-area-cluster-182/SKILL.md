---
name: gitnexus-area-cluster-182
description: "Skill for the Cluster_182 area of OpenCBP_FO4. 7 symbols across 3 files."
---

# Cluster_182

7 symbols | 3 files | Cohesion: 100%

## When to Use

- Working with code in `common/`
- Understanding how _FATALERROR, _AssertionFailed, _AssertionFailed_ErrCode work
- Modifying cluster_182-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `common/IErrors.cpp` | IErrors_Halt, _AssertionFailed, _AssertionFailed_ErrCode |
| `common/IDebugLog.h` | _FATALERROR, Log |
| `common/IDebugLog.cpp` | Open, OpenRelative |

## Entry Points

Start here when exploring this area:

- **`_FATALERROR`** (Function) — `common/IDebugLog.h:80`
- **`_AssertionFailed`** (Function) — `common/IErrors.cpp:17`
- **`_AssertionFailed_ErrCode`** (Function) — `common/IErrors.cpp:32`
- **`Open`** (Method) — `common/IDebugLog.cpp:33`
- **`OpenRelative`** (Method) — `common/IDebugLog.cpp:54`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `_FATALERROR` | Function | `common/IDebugLog.h` | 80 |
| `_AssertionFailed` | Function | `common/IErrors.cpp` | 17 |
| `_AssertionFailed_ErrCode` | Function | `common/IErrors.cpp` | 32 |
| `Open` | Method | `common/IDebugLog.cpp` | 33 |
| `OpenRelative` | Method | `common/IDebugLog.cpp` | 54 |
| `Log` | Method | `common/IDebugLog.h` | 34 |
| `IErrors_Halt` | Function | `common/IErrors.cpp` | 4 |

## How to Explore

1. `context({name: "_FATALERROR"})` — see callers and callees
2. `query({search_query: "cluster_182"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
