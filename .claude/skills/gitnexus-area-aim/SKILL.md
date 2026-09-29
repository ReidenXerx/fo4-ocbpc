---
name: gitnexus-area-aim
description: "Skill for the Aim area of OpenCBP_FO4. 9 symbols across 2 files."
---

# Aim

9 symbols | 2 files | Cohesion: 64%

## When to Use

- Working with code in `tests/`
- Understanding how Pose, ToMatrix, Update work
- Modifying aim-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `tests/aim/aim_test.cpp` | Written, main, at, compose, grip (+1) |
| `CBPSSE/AimSolve.h` | Pose, ToMatrix, Update |

## Entry Points

Start here when exploring this area:

- **`Pose`** (Function) — `CBPSSE/AimSolve.h:96`
- **`ToMatrix`** (Function) — `CBPSSE/AimSolve.h:55`
- **`Update`** (Function) — `CBPSSE/AimSolve.h:186`
- **`main`** (Function) — `tests/aim/aim_test.cpp:87`
- **`at`** (Function) — `tests/aim/aim_test.cpp:146`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Pose` | Function | `CBPSSE/AimSolve.h` | 96 |
| `ToMatrix` | Function | `CBPSSE/AimSolve.h` | 55 |
| `Update` | Function | `CBPSSE/AimSolve.h` | 186 |
| `main` | Function | `tests/aim/aim_test.cpp` | 87 |
| `at` | Function | `tests/aim/aim_test.cpp` | 146 |
| `compose` | Function | `tests/aim/aim_test.cpp` | 415 |
| `grip` | Function | `tests/aim/aim_test.cpp` | 366 |
| `ring` | Function | `tests/aim/aim_test.cpp` | 358 |
| `Written` | Function | `tests/aim/aim_test.cpp` | 52 |

## How to Explore

1. `context({name: "Pose"})` — see callers and callees
2. `query({search_query: "aim"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
