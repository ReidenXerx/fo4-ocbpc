---
name: gitnexus-area-cluster-327
description: "Skill for the Cluster_327 area of OpenCBP_FO4. 6 symbols across 1 files."
---

# Cluster_327

6 symbols | 1 files | Cohesion: 100%

## When to Use

- Working with code in `common/`
- Understanding how Message, OpenBlock, PrintSpaces work
- Modifying cluster_327-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `common/IDebugLog.cpp` | Message, OpenBlock, PrintSpaces, PrintText, SeekCursor (+1) |

## Entry Points

Start here when exploring this area:

- **`Message`** (Method) — `common/IDebugLog.cpp:78`
- **`OpenBlock`** (Method) — `common/IDebugLog.cpp:201`
- **`PrintSpaces`** (Method) — `common/IDebugLog.cpp:233`
- **`PrintText`** (Method) — `common/IDebugLog.cpp:260`
- **`SeekCursor`** (Method) — `common/IDebugLog.cpp:303`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Message` | Method | `common/IDebugLog.cpp` | 78 |
| `OpenBlock` | Method | `common/IDebugLog.cpp` | 201 |
| `PrintSpaces` | Method | `common/IDebugLog.cpp` | 233 |
| `PrintText` | Method | `common/IDebugLog.cpp` | 260 |
| `SeekCursor` | Method | `common/IDebugLog.cpp` | 303 |
| `TabSize` | Method | `common/IDebugLog.cpp` | 312 |

## How to Explore

1. `context({name: "Message"})` — see callers and callees
2. `query({search_query: "cluster_327"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
