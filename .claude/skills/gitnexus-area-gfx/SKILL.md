---
name: gitnexus-area-gfx
description: "Skill for the GFx area of OpenCBP_FO4. 48 symbols across 9 files."
---

# GFx

48 symbols | 9 files | Cohesion: 88%

## When to Use

- Working with code in `extern/`
- Understanding how Value, GASRefCountBase, RefCountBaseGC work
- Modifying gfx-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | Value, AcquireManagedValue, IsManagedValue, ReleaseManagedValue, Value (+26) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | GASRefCountBase, RefCountBaseGC, VM, VMFile, MovieRoot |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | StateBag, State, Translator, GetStateBagImpl, SetState |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Resource.h` | FileTypeConstants, Resource |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | NewOverrideBase |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_ASMovieRootBase.h` | ASMovieRootBase |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | RefCountBase |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Render/Render_ThreadCommandQueue.h` | ThreadCommand |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Log.h` | LogState |

## Entry Points

Start here when exploring this area:

- **`Value`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h:146`
- **`GASRefCountBase`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h:238`
- **`RefCountBaseGC`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h:212`
- **`VM`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h:406`
- **`VMFile`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h:611`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Value` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 146 |
| `GASRefCountBase` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | 238 |
| `RefCountBaseGC` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | 212 |
| `VM` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | 406 |
| `VMFile` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | 611 |
| `NewOverrideBase` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_Memory.h` | 217 |
| `StateBag` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | 95 |
| `Movie` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 700 |
| `MovieDef` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 59 |
| `FileTypeConstants` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Resource.h` | 20 |
| `Resource` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Resource.h` | 8 |
| `MovieRoot` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_AS3.h` | 362 |
| `ASMovieRootBase` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_ASMovieRootBase.h` | 40 |
| `RefCountBase` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Kernel/SF_RefCount.h` | 61 |
| `ThreadCommand` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/Render/Render_ThreadCommandQueue.h` | 11 |
| `State` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | 13 |
| `Translator` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Loader.h` | 86 |
| `LogState` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Log.h` | 19 |
| `AcquireManagedValue` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 641 |
| `IsManagedValue` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Scaleform/GFx/GFx_Player.h` | 648 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Operator= → ObjectAddRef` | intra_community | 3 |
| `Operator= → ObjectRelease` | intra_community | 3 |

## How to Explore

1. `context({name: "Value"})` — see callers and callees
2. `query({search_query: "gfx"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
