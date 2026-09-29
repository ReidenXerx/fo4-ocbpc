---
name: gitnexus-area-bethesda
description: "Skill for the Bethesda area of OpenCBP_FO4. 363 symbols across 69 files."
---

# Bethesda

363 symbols | 69 files | Cohesion: 83%

## When to Use

- Working with code in `extern/`
- Understanding how aligned_alloc, aligned_free, malloc work
- Modifying bethesda-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | BGSAttachParentArray, BGSAttackDataForm, BGSBipedObjectForm, BGSBlockBashData, BGSCraftingUseSound (+48) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/TESForms.h` | BGSBodyPartData, BGSCameraShot, BGSConstructibleObject, BGSLocation, BGSMessage (+35) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/TESBoundObjects.h` | BGSAddonNode, BGSArtObject, BGSComponent, BGSExplosion, BGSHazard (+18) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSTHashMap.h` | clear, do_erase, empty, get_entries, get_entry_for (+16) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSTArray.h` | BSTArray, BSTArray, begin, end, erase (+14) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/IMenu.h` | ContainerMenuBase, GameMenuBase, MessageBoxMenu, PipboyMenu, PipboySubMenu (+13) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSFixedString.h` | back, c_str, contains, data, empty (+8) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/TESBoundAnimObjects.h` | TESActorBase, TESBoundAnimObject, TESFlora, TESFurniture, TESLevCharacter (+5) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSTList.h` | assign, cbefore_begin, cend, clear, erase_after (+5) |
| `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BSStringT.h` | BSStringT, BSStringT, copy_from, length, move_from (+3) |

## Entry Points

Start here when exploring this area:

- **`aligned_alloc`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/MemoryManager.h:277`
- **`aligned_free`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/MemoryManager.h:322`
- **`malloc`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/MemoryManager.h:265`
- **`calloc`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/MemoryManager.h:289`
- **`free`** (Function) — `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/MemoryManager.h:316`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `ActorValueInfo` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/ActorValueInfo.h` | 203 |
| `BGSHeadPart` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BGSHeadPart.h` | 10 |
| `Mod` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BGSMod.h` | 17 |
| `Container` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BGSMod.h` | 103 |
| `Item` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/BGSMod.h` | 247 |
| `BGSAttachParentArray` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 19 |
| `BGSAttackDataForm` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 20 |
| `BGSBipedObjectForm` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 21 |
| `BGSBlockBashData` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 22 |
| `BGSCraftingUseSound` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 23 |
| `BGSDestructibleObjectForm` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 24 |
| `BGSEquipType` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 25 |
| `BGSFeaturedItemMessage` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 26 |
| `BGSForcedLocRefType` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 27 |
| `BGSIdleCollection` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 28 |
| `BGSInstanceNamingRulesForm` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 29 |
| `BGSKeywordForm` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 30 |
| `BGSMenuDisplayObject` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 31 |
| `BGSMessageIcon` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 32 |
| `BGSModelMaterialSwap` | Class | `extern/CommonLibF4RD/CommonLibF4/include/RE/Bethesda/FormComponents.h` | 48 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `UpdateEyeProbe → Get` | cross_community | 4 |
| `InstallFrame → Get` | cross_community | 4 |
| `Operator== → Data` | intra_community | 3 |
| `Operator== → Size` | intra_community | 3 |
| `Reserve → Size` | intra_community | 3 |
| `GetItemIndex → Begin` | intra_community | 3 |
| `Resolve → Get` | cross_community | 3 |

## How to Explore

1. `context({name: "aligned_alloc"})` — see callers and callees
2. `query({search_query: "bethesda"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
