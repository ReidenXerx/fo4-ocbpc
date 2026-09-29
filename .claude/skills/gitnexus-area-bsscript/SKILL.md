---
name: gitnexus-area-bsscript
description: "Skill for the BSScript area of OpenCBP_FO4. 10 symbols across 5 files."
---

# BSScript

10 symbols | 5 files | Cohesion: 100%

## When to Use

- Working with code in `extern/`
- Understanding how IFunction, NativeFunctionBase, NativeFunction work
- Modifying bsscript-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | IsArray, IsComplex, IsComplexTypeArray, SetArray |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | copy, operator=, reset |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/IFunction.h` | IFunction |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/NF_util/NativeFunctionBase.h` | NativeFunctionBase |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScriptUtil.h` | NativeFunction |

## Entry Points

Start here when exploring this area:

- **`IFunction`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/IFunction.h:31`
- **`NativeFunctionBase`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/NF_util/NativeFunctionBase.h:33`
- **`NativeFunction`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScriptUtil.h:1123`
- **`IsArray`** (Method) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h:82`
- **`IsComplex`** (Method) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h:91`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `IFunction` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/IFunction.h` | 31 |
| `NativeFunctionBase` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/NF_util/NativeFunctionBase.h` | 33 |
| `NativeFunction` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScriptUtil.h` | 1123 |
| `IsArray` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 82 |
| `IsComplex` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 91 |
| `IsComplexTypeArray` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 101 |
| `SetArray` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/TypeInfo.h` | 111 |
| `copy` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 205 |
| `operator=` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 43 |
| `reset` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSScript/Variable.h` | 198 |

## How to Explore

1. `context({name: "IFunction"})` — see callers and callees
2. `query({search_query: "bsscript"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
