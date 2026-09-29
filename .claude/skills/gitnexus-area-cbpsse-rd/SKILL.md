---
name: gitnexus-area-cbpsse-rd
description: "Skill for the CBPSSE-RD area of OpenCBP_FO4. 140 symbols across 29 files."
---

# CBPSSE-RD

140 symbols | 29 files | Cohesion: 69%

## When to Use

- Working with code in `CBPSSE-RD/`
- Understanding how EyeClockMs, EyeLidMax, RefreshHeldFaces work
- Modifying cbpsse-rd-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `CBPSSE-RD/Mouth.cpp` | EndSpeech, FaceData, HeldFaceData, HeldOnly, Publish (+21) |
| `CBPSSE-RD/Aim.cpp` | LoadAimConfig, ParsePoint, ReadPath, ReadPoint, Split (+13) |
| `CBPSSE-RD/Eyes.cpp` | InstallEyeHookUnguarded, ActorOf, guard, guard, EyeProperty (+11) |
| `CBPSSE-RD/Bones.cpp` | EnsureAnatomyBonesImpl, IsOurs, LoadBonesConfig, Lower, Note (+4) |
| `CBPSSE-RD/config.cpp` | DumpConfigsToLog, DumpWhitelistToLog, configReader, ReadBoneSections, DumpCollisionConfigsToLog (+2) |
| `CBPSSE-RD/ActorUtils.cpp` | GetActorRaceEID, IsActorInPowerArmor, IsActorMale, IsActorTorsoArmorEquipped, IsActorTrackable (+2) |
| `CBPSSE-RD/Hook.cpp` | CallSite, EnsureTrampoline, Id, InstallFrame, Known (+1) |
| `CBPSSE-RD/Thing.cpp` | Reset, IsActorValid, ShowPos, ShowRot, Update (+1) |
| `CBPSSE-RD/CollisionHub.cpp` | AddPropColliders, AnatomyNote, AnatomyScan, CreateOtherColliders, AnatomyLog (+1) |
| `CBPSSE-RD/Hook.h` | CallSite, OgFamily, Resolve, WriteCall |

## Entry Points

Start here when exploring this area:

- **`EyeClockMs`** (Function) — `CBPSSE-RD/Eyes.h:31`
- **`EyeLidMax`** (Function) — `CBPSSE-RD/Eyes.h:30`
- **`RefreshHeldFaces`** (Function) — `CBPSSE-RD/Mouth.cpp:1180`
- **`UpdateMouths`** (Function) — `CBPSSE-RD/Mouth.cpp:706`
- **`knobOn`** (Function) — `CBPSSE-RD/Mouth.cpp:749`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `EyeClockMs` | Function | `CBPSSE-RD/Eyes.h` | 31 |
| `EyeLidMax` | Function | `CBPSSE-RD/Eyes.h` | 30 |
| `RefreshHeldFaces` | Function | `CBPSSE-RD/Mouth.cpp` | 1180 |
| `UpdateMouths` | Function | `CBPSSE-RD/Mouth.cpp` | 706 |
| `knobOn` | Function | `CBPSSE-RD/Mouth.cpp` | 749 |
| `Snapshot` | Function | `CBPSSE/FaceAuthority.h` | 199 |
| `DumpConfigsToLog` | Function | `CBPSSE-RD/config.cpp` | 607 |
| `DumpWhitelistToLog` | Function | `CBPSSE-RD/config.cpp` | 627 |
| `EyesTurn` | Function | `CBPSSE-RD/Eyes.h` | 29 |
| `InstallMouthHook` | Function | `CBPSSE-RD/Mouth.cpp` | 673 |
| `ListenForFaces` | Function | `CBPSSE-RD/Mouth.cpp` | 1126 |
| `LoadFaceConfig` | Function | `CBPSSE-RD/Mouth.cpp` | 599 |
| `SayFaceHello` | Function | `CBPSSE-RD/Mouth.cpp` | 1145 |
| `LoadConfig` | Function | `CBPSSE-RD/config.h` | 49 |
| `F4SEPlugin_Load` | Function | `CBPSSE-RD/main.cpp` | 101 |
| `UpdateActors` | Function | `CBPSSE-RD/scan.cpp` | 106 |
| `GetMessagingInterface` | Function | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/API.h` | 27 |
| `Init` | Function | `extern/CommonLibF4RD/CommonLibF4/include/F4SE/API.h` | 20 |
| `CallSite` | Function | `CBPSSE-RD/Hook.h` | 19 |
| `OgFamily` | Function | `CBPSSE-RD/Hook.h` | 15 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `EnsureLoadedActors → AnatomyLogLine` | cross_community | 5 |
| `LoadAimConfig → Split` | intra_community | 4 |
| `EnsureLoadedActors → Reaches` | cross_community | 4 |
| `EnsureLoadedActors → VisitGeometry` | cross_community | 4 |
| `EnsureLoadedActors → CheckSkin` | cross_community | 4 |
| `EnsureLoadedActors → Skin` | cross_community | 4 |
| `UpdateEyeProbe → Get` | cross_community | 4 |
| `InstallFrame → OgFamily` | cross_community | 4 |
| `LoadMouthConfig → Dot` | cross_community | 4 |
| `InstallFrame → Get` | cross_community | 4 |

## How to Explore

1. `context({name: "EyeClockMs"})` — see callers and callees
2. `query({search_query: "cbpsse-rd"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
