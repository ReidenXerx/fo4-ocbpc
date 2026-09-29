---
name: gitnexus-area-havok
description: "Skill for the Havok area of OpenCBP_FO4. 12 symbols across 8 files."
---

# Havok

12 symbols | 8 files | Cohesion: 100%

## When to Use

- Working with code in `extern/`
- Understanding how hkBaseObject, hkReferencedObject, hknpAllHitsCollector work
- Modifying havok-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemoryAllocator.h` | BlockAlloc, BlockFree, BufAlloc, BufFree, BufRealloc |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkBaseObject.h` | hkBaseObject |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkReferencedObject.h` | hkReferencedObject |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpAllHitsCollector.h` | hknpAllHitsCollector |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpCollisionQueryCollector.h` | hknpCollisionQueryCollector |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpUniqueBodyIdHitCollector.h` | hknpUniqueBodyIdHitCollector |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemoryRouter.h` | GetInstance |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemorySystem.h` | GarbageCollect |

## Entry Points

Start here when exploring this area:

- **`hkBaseObject`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkBaseObject.h:4`
- **`hkReferencedObject`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkReferencedObject.h:8`
- **`hknpAllHitsCollector`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpAllHitsCollector.h:8`
- **`hknpCollisionQueryCollector`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpCollisionQueryCollector.h:9`
- **`hknpUniqueBodyIdHitCollector`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpUniqueBodyIdHitCollector.h:9`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `hkBaseObject` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkBaseObject.h` | 4 |
| `hkReferencedObject` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkReferencedObject.h` | 8 |
| `hknpAllHitsCollector` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpAllHitsCollector.h` | 8 |
| `hknpCollisionQueryCollector` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpCollisionQueryCollector.h` | 9 |
| `hknpUniqueBodyIdHitCollector` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hknpUniqueBodyIdHitCollector.h` | 9 |
| `BlockAlloc` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemoryAllocator.h` | 16 |
| `BlockFree` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemoryAllocator.h` | 17 |
| `BufAlloc` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemoryAllocator.h` | 18 |
| `BufFree` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemoryAllocator.h` | 19 |
| `BufRealloc` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemoryAllocator.h` | 21 |
| `GarbageCollect` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemorySystem.h` | 50 |
| `GetInstance` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Havok/hkMemoryRouter.h` | 14 |

## How to Explore

1. `context({name: "hkBaseObject"})` — see callers and callees
2. `query({search_query: "havok"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
