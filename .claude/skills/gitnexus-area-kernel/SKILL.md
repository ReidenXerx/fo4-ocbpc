---
name: gitnexus-area-kernel
description: "Skill for the Kernel area of OpenCBP_FO4. 21 symbols across 5 files."
---

# Kernel

21 symbols | 5 files | Cohesion: 92%

## When to Use

- Working with code in `extern/`
- Understanding how calloc, malloc, operator== work
- Modifying kernel-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | Ptr, TryAttach, TryDetach, operator=, reset (+2) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | calloc, malloc, Alloc, Free, GetGlobalHeap (+1) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | AcquireInterface, Event, Mutex, Waitable |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | AllocatorPagedCC, ConstructorMov, ConstructorPagedMovCC |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/IMenu.h` | ProcessMessage |

## Entry Points

Start here when exploring this area:

- **`calloc`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h:137`
- **`malloc`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h:115`
- **`operator==`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h:268`
- **`Ptr`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h:70`
- **`AcquireInterface`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h:10`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Ptr` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 70 |
| `AcquireInterface` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 10 |
| `Event` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 11 |
| `Mutex` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 12 |
| `Waitable` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Threads.h` | 14 |
| `AllocatorPagedCC` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | 36 |
| `ConstructorMov` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | 21 |
| `ConstructorPagedMovCC` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Allocator.h` | 28 |
| `calloc` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 137 |
| `malloc` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 115 |
| `operator==` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 268 |
| `TryAttach` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 237 |
| `TryDetach` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 244 |
| `operator=` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 140 |
| `reset` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 193 |
| `ProcessMessage` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/IMenu.h` | 245 |
| `get` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 211 |
| `Alloc` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 38 |
| `Free` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 103 |
| `GetGlobalHeap` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 17 |

## How to Explore

1. `context({name: "calloc"})` — see callers and callees
2. `query({search_query: "kernel"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
