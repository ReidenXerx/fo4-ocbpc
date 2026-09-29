---
name: gitnexus-area-f4se
description: "Skill for the F4se area of OpenCBP_FO4. 347 symbols across 81 files."
---

# F4se

347 symbols | 81 files | Cohesion: 84%

## When to Use

- Working with code in `f4se/`
- Understanding how code, code, code work
- Modifying f4se-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `f4se/f4se/ScaleformValue.h` | GetString, SetBool, SetNumber, BSGFxDisplayObject, GetBool (+23) |
| `f4se/f4se/GameTypes.h` | Release, Release, Lock, Lock, Iterator (+19) |
| `f4se/f4se/PapyrusArgs.h` | GetExtraData, GetValue, IsNone, GetIdentifier, Get (+10) |
| `f4se/f4se/GameInput.h` | BSGamepadDevice, BSGamepadDeviceDelegate, BSInputDevice, BSKeyboardDevice, BSMouseDevice (+10) |
| `f4se/f4se/Hooks_Scaleform.cpp` | Hooks_Scaleform_Commit, code, code, code, BSScaleformManager_Code (+8) |
| `f4se/f4se/PapyrusEvents.h` | RemoveFilter, Unregister, AddFilter, Register, Save (+8) |
| `f4se/f4se/PapyrusInstanceData.cpp` | GetArmorInstanceData, GetDamageTypes, GetGoldValue, GetInstanceData, GetKeywords (+6) |
| `f4se/f4se/PapyrusScriptObject.cpp` | UnregisterForCameraState, UnregisterForFurnitureEvent, RegisterForCameraState, RegisterForFurnitureEvent, RegisterForControl (+5) |
| `f4se/f4se/Hooks_Papyrus.cpp` | code, code, code, code, DelayFunctorQueue_Code (+3) |
| `f4se/f4se/Hooks_SaveLoad.cpp` | code, code, code, DeleteSaveGame_Code, LoadGame_Code (+3) |

## Entry Points

Start here when exploring this area:

- **`code`** (Function) — `f4se/f4se/Hooks_Camera.cpp:56`
- **`code`** (Function) — `f4se/f4se/Hooks_Input.cpp:248`
- **`code`** (Function) — `f4se/f4se/Hooks_Input.cpp:273`
- **`Hooks_Memory_Commit`** (Function) — `f4se/f4se/Hooks_Memory.cpp:42`
- **`code`** (Function) — `f4se/f4se/Hooks_Papyrus.cpp:228`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `BSGamepadDevice` | Class | `f4se/f4se/GameInput.h` | 194 |
| `BSGamepadDeviceDelegate` | Class | `f4se/f4se/GameInput.h` | 199 |
| `BSInputDevice` | Class | `f4se/f4se/GameInput.h` | 124 |
| `BSKeyboardDevice` | Class | `f4se/f4se/GameInput.h` | 156 |
| `BSMouseDevice` | Class | `f4se/f4se/GameInput.h` | 178 |
| `BSVirtualKeyboardDevice` | Class | `f4se/f4se/GameInput.h` | 163 |
| `NiExtraData` | Class | `f4se/f4se/NiExtraData.h` | 8 |
| `NiObject` | Class | `f4se/f4se/NiObjects.h` | 44 |
| `StaticTexture` | Class | `f4se/f4se/NiTextures.h` | 44 |
| `BSTextureSet` | Class | `f4se/f4se/NiTextures.h` | 64 |
| `NiTexture` | Class | `f4se/f4se/NiTextures.h` | 26 |
| `bhkWorld` | Class | `f4se/f4se/bhkWorld.h` | 5 |
| `NiCollisionObject` | Class | `f4se/f4se/BSCollision.h` | 9 |
| `bhkCollisionObject` | Class | `f4se/f4se/BSCollision.h` | 47 |
| `bhkNPCollisionObject` | Class | `f4se/f4se/BSCollision.h` | 79 |
| `bhkNPCollisionObjectBase` | Class | `f4se/f4se/BSCollision.h` | 16 |
| `bhkNiCollisionObject` | Class | `f4se/f4se/BSCollision.h` | 23 |
| `CustomMenu` | Class | `f4se/f4se/CustomMenu.h` | 25 |
| `GameMenuBase` | Class | `f4se/f4se/GameMenus.h` | 140 |
| `IMenu` | Class | `f4se/f4se/GameMenus.h` | 44 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Dump → Item` | cross_community | 4 |
| `Dump → Next` | cross_community | 4 |
| `Dump → Head` | cross_community | 4 |
| `BSScaleformTint_Hook → GetType` | cross_community | 3 |
| `Core_LoadCallback → Load` | cross_community | 3 |
| `Core_LoadCallback → Release` | cross_community | 3 |
| `PackHandle → AddRef` | intra_community | 3 |
| `GetInstalledLightPlugins → Heap_Free` | cross_community | 3 |
| `RegisterForFurnitureEvent → Lock` | intra_community | 3 |
| `Index → GetType` | cross_community | 3 |

## How to Explore

1. `context({name: "code"})` — see callers and callees
2. `query({search_query: "f4se"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
