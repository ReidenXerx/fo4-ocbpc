---
name: gitnexus-area-face
description: "Skill for the Face area of OpenCBP_FO4. 9 symbols across 3 files."
---

# Face

9 symbols | 3 files | Cohesion: 65%

## When to Use

- Working with code in `tests/`
- Understanding how CurrentKnobs, AfterMerge, main work
- Modifying face-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `tests/face/face_test.cpp` | main, hold, run, shown, Blink (+2) |
| `CBPSSE/FaceAuthority.h` | CurrentKnobs |
| `CBPSSE/FaceCompose.h` | AfterMerge |

## Entry Points

Start here when exploring this area:

- **`CurrentKnobs`** (Function) — `CBPSSE/FaceAuthority.h:205`
- **`AfterMerge`** (Function) — `CBPSSE/FaceCompose.h:86`
- **`main`** (Function) — `tests/face/face_test.cpp:151`
- **`hold`** (Function) — `tests/face/face_test.cpp:914`
- **`run`** (Function) — `tests/face/face_test.cpp:519`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `CurrentKnobs` | Function | `CBPSSE/FaceAuthority.h` | 205 |
| `AfterMerge` | Function | `CBPSSE/FaceCompose.h` | 86 |
| `main` | Function | `tests/face/face_test.cpp` | 151 |
| `hold` | Function | `tests/face/face_test.cpp` | 914 |
| `run` | Function | `tests/face/face_test.cpp` | 519 |
| `shown` | Function | `tests/face/face_test.cpp` | 895 |
| `Blink` | Method | `tests/face/face_test.cpp` | 47 |
| `Merge` | Method | `tests/face/face_test.cpp` | 87 |
| `Frame` | Method | `tests/face/face_test.cpp` | 116 |

## How to Explore

1. `context({name: "CurrentKnobs"})` — see callers and callees
2. `query({search_query: "face"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
