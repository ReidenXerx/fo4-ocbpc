---
name: gitnexus-area-netimmerse
description: "Skill for the NetImmerse area of OpenCBP_FO4. 19 symbols across 9 files."
---

# NetImmerse

19 symbols | 9 files | Cohesion: 97%

## When to Use

- Working with code in `extern/`
- Understanding how operator<=>, operator==, NiAVObject work
- Modifying netimmerse-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | NiPointer, TryAttach, TryDetach, operator=, reset (+3) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiTArray.h` | operator++, operator--, slot_filled, validate |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiAVObject.h` | NiAVObject |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiNode.h` | NiNode |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiObjectNET.h` | NiObjectNET |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiProperty.h` | NiProperty |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiShadeProperty.h` | NiShadeProperty |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiBinaryStream.h` | NiBinaryStream |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiFile.h` | NiFile |

## Entry Points

Start here when exploring this area:

- **`operator<=>`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h:169`
- **`operator==`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h:162`
- **`NiAVObject`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiAVObject.h:28`
- **`NiNode`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiNode.h:9`
- **`NiObjectNET`** (Class) — `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiObjectNET.h:18`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `NiAVObject` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiAVObject.h` | 28 |
| `NiNode` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiNode.h` | 9 |
| `NiObjectNET` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiObjectNET.h` | 18 |
| `NiProperty` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiProperty.h` | 8 |
| `NiShadeProperty` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiShadeProperty.h` | 6 |
| `NiPointer` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 7 |
| `NiBinaryStream` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiBinaryStream.h` | 4 |
| `NiFile` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiFile.h` | 7 |
| `operator<=>` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 169 |
| `operator==` | Function | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 162 |
| `TryAttach` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 134 |
| `TryDetach` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 141 |
| `operator=` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 58 |
| `reset` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 100 |
| `operator++` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiTArray.h` | 78 |
| `operator--` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiTArray.h` | 88 |
| `slot_filled` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiTArray.h` | 114 |
| `validate` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiTArray.h` | 106 |
| `get` | Method | `extern/CommonLibF4RD/CommonLibF4/include/RE/NetImmerse/NiSmartPointer.h` | 114 |

## How to Explore

1. `context({name: "operator<=>"})` — see callers and callees
2. `query({search_query: "netimmerse"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
