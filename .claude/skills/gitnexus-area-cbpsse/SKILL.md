---
name: gitnexus-area-cbpsse
description: "Skill for the CBPSSE area of OpenCBP_FO4. 195 symbols across 34 files."
---

# CBPSSE

195 symbols | 34 files | Cohesion: 64%

## When to Use

- Working with code in `CBPSSE/`
- Understanding how Add, Bend, ChainLength work
- Modifying cbpsse-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `CBPSSE/Mouth.cpp` | EndSpeech, FaceData, HeldFaceData, HeldOnly, UpdateMouths (+24) |
| `CBPSSE/Aim.cpp` | PutBack, UpdateAims, UpdateWorldFrom, AddGripTargets, add (+17) |
| `CBPSSE/AimSolve.cpp` | Add, Bend, ChainLength, Dot, FromTo (+16) |
| `CBPSSE/Eyes.cpp` | Dot, EyesOf, Length, Toward, UpdateEyeProbe (+10) |
| `CBPSSE/FaceAuthority.h` | TestFace, Decode, SetDeep, SetGlanceFace, SetGlance (+8) |
| `CBPSSE/AimSolve.h` | ChainLength, FromMatrix, Judge, Length, HeadFor (+7) |
| `CBPSSE/Bones.cpp` | EnsureAnatomyBonesImpl, IsOurs, LoadBonesConfig, Lower, Note (+4) |
| `CBPSSE/config.cpp` | DumpConfigsToLog, DumpWhitelistToLog, configReader, ReadBoneSections, DumpCollisionConfigsToLog (+2) |
| `CBPSSE/ActorUtils.cpp` | GetActorRaceEID, IsActorInPowerArmor, IsActorTorsoArmorEquipped, IsActorValid, IsActorMale (+2) |
| `CBPSSE/CollisionHub.cpp` | AddPropColliders, AnatomyNote, AnatomyScan, CreateOtherColliders, AnatomyLog (+1) |

## Entry Points

Start here when exploring this area:

- **`Add`** (Function) — `CBPSSE/AimSolve.cpp:10`
- **`Bend`** (Function) — `CBPSSE/AimSolve.cpp:289`
- **`ChainLength`** (Function) — `CBPSSE/AimSolve.cpp:152`
- **`Dot`** (Function) — `CBPSSE/AimSolve.cpp:13`
- **`FromTo`** (Function) — `CBPSSE/AimSolve.cpp:47`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Add` | Function | `CBPSSE/AimSolve.cpp` | 10 |
| `Bend` | Function | `CBPSSE/AimSolve.cpp` | 289 |
| `ChainLength` | Function | `CBPSSE/AimSolve.cpp` | 152 |
| `Dot` | Function | `CBPSSE/AimSolve.cpp` | 13 |
| `FromTo` | Function | `CBPSSE/AimSolve.cpp` | 47 |
| `GripCentre` | Function | `CBPSSE/AimSolve.cpp` | 253 |
| `Judge` | Function | `CBPSSE/AimSolve.cpp` | 160 |
| `Length` | Function | `CBPSSE/AimSolve.cpp` | 15 |
| `Normalized` | Function | `CBPSSE/AimSolve.cpp` | 16 |
| `Oriented` | Function | `CBPSSE/AimSolve.cpp` | 212 |
| `Scale` | Function | `CBPSSE/AimSolve.cpp` | 12 |
| `Slerp` | Function | `CBPSSE/AimSolve.cpp` | 64 |
| `Sub` | Function | `CBPSSE/AimSolve.cpp` | 11 |
| `allowed` | Function | `CBPSSE/AimSolve.cpp` | 367 |
| `Update` | Function | `CBPSSE/AimSolve.cpp` | 344 |
| `EyeLidMax` | Function | `CBPSSE/Eyes.h` | 29 |
| `TestFace` | Function | `CBPSSE/FaceAuthority.h` | 186 |
| `UpdateMouths` | Function | `CBPSSE/Mouth.cpp` | 712 |
| `knobOn` | Function | `CBPSSE/Mouth.cpp` | 755 |
| `Decode` | Function | `CBPSSE/FaceAuthority.h` | 167 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `EnsureLoadedActors → Lower` | cross_community | 5 |
| `EnsureLoadedActors → AnatomyLogLine` | cross_community | 5 |
| `LoadAimConfig → Split` | intra_community | 4 |
| `Judge → Dot` | intra_community | 4 |
| `EnsureLoadedActors → Reaches` | cross_community | 4 |
| `EnsureLoadedActors → VisitGeometry` | cross_community | 4 |
| `LoadMouthConfig → Dot` | cross_community | 4 |
| `Pose → Conj` | intra_community | 3 |
| `Pose → Mul` | intra_community | 3 |
| `Update → Add` | intra_community | 3 |

## How to Explore

1. `context({name: "Add"})` — see callers and callees
2. `query({search_query: "cbpsse"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
