# fo4-ocbpc

OCBPC (OpenCBP physics with collisions, for Fallout 4), extended for
[Anatomy](https://github.com/ReidenXerx/fo4-anatomy) and Rapport. This is the source of the
`cbp.dll` released as its own mod, a drop-in replacement for OCBPC's: your `ocbp.ini` and
`OCBPCollisionConfig.txt` keep working unchanged.

It is a fork of [ericncream/OpenCBP_FO4](https://github.com/ericncream/OpenCBP_FO4) at commit
`abc0192` (2020-04-13), the code behind the OCBPC 0.3 release. That in turn derives from OpenCBP
by JS.

Game: Fallout 4 1.10.163 (Steam and GOG) with F4SE 0.6.23. Every hook reads the engine code it
patches first, and stays off, logged, on any other build.

## What it adds

The complete list, with what each needs, is in **[docs/FEATURES.md](docs/FEATURES.md)**. In short:

- **Collision:** each collider pushes once per frame (OCBPC pushed twice near a grid-cell edge, for
  every config); a penis and a held toy collide as one smooth tube, not a string of balls; held props
  are colliders, limited to the bones you name; stretch groups let an opening widen for something
  bigger.
- **Bones at run time** (`[Bones]`): genital bones are added under each actor's `Pelvis_skin`, so no
  skeleton is patched or shipped.
- **Aim and shape** (`[Aim]`, `[Shape]`): in AAF scenes a penis finds the opening the animation
  meant, bends along the canal or down the throat, follows a gripping hand, and takes a thinner shaft
  and a bigger head, per man.
- **The mouth** (`[Mouth]`): it opens to what is at the lips, with the lips fitted to the
  cross-section of what is inside, corners that open out or hug, and a face that reacts.
- **Rapport's faces** (`[Face]`): a face Rapport holds is written after the engine's merge, so it can
  close eyelids and a jaw an animation opened; lip sync keeps the mouth while a line plays; faces
  ease in and out; the brows follow penetration depth; partners glance into each other's eyes
  (`[Eyes]`); Rapport's MCM tunes the engine live.
- **Stability:** guarded, self-checking hooks; a log a second game process cannot truncate; a
  discovery log (`Documents\My Games\Fallout4\F4SE\anatomy_ocbpc.log`) for new bodies, props and
  creatures.

## Configuration

Two ini files are read, yours first: `Data\F4SE\Plugins\ocbp.ini`, then
`Data\F4SE\Plugins\Anatomy\ocbp.ini`. The sections below come from Anatomy's file when it has
them, otherwise from yours. `OCBPCollisionConfig.txt` is read the same way and appended.

| section | what it turns on | off unless |
| --- | --- | --- |
| `[Attach]`, bone sections, `[Override:*]`, `[Whitelist]` | OCBPC's physics, as always | - |
| `[Props]` | held props as colliders (`nodes`, `radius`, `spacing`, `maxLength`, `minBound`, `targets`) | `nodes` is set |
| `[Bones]` | run-time bones, `name=parent,x,y,z` under `Pelvis_skin` | listed |
| `[Tube]` | penis chains (and props, `props=1`) as one tube; `skin`, `glans`, `glansProfile` | `enabled=1` |
| `[Aim]` | the aim: `chain`, openings (`vagina`, `anus`, paths, `throatF/M`, `throatNeckF/M`), angles and reach | `enabled=1` |
| `[Shape]` | `shaft`, `headMin`, `headMax` | `enabled=1` |
| `[Mouth]` | the contact mouth, the lip table (`lip<F/M><id>`), corners, glans, `face=` reaction terms | `enabled=1` |
| `[Face]` | `authority` (Rapport's faces), `react` | on by default |
| `[Eyes]` | the eye hook; `glances=1` tells Rapport glances work; `uMax`, `vMin`, `vMax`, `rollMax`, `axes` | `enabled` on by default, `glances` off |
| `[Log]` | `discover=1`: log genital- or toy-looking nodes on nearby actors (a modder's tool; walks scene graphs every 2 s) | `discover=1` |

Each key's default, clamp and meaning is in the header comment of its source file (`Mouth.h`,
`Aim.h`, `Eyes.h`, `Bones.h`, `TubeCollide.h`, `FaceAuthority.h`) and in `config.cpp`. `probe=` and
`test=` keys are for development and log heavily.

The messages Rapport and the engine exchange (`RFAS`, `RFAC`, `RFAD`, `RFAK`, `RFAG`, `RFAX`, and
the engine's `RFAH` hello) are defined in `CBPSSE/FaceAuthority.h`.

Each source file this fork added or changed says so in its first lines, with the date and what
changed. `git log abc0192..` has the full history, one reasoned commit per change.

## Tests

`tests/aim`, `tests/face`, `tests/lips` and `tests/tube` are offline suites (`run.bat` in each),
built with the same toolset. Each was proven by planted mutations: every planted fault must fail it.

## Build

Visual Studio 2022 Build Tools (v143) and the Windows 10 SDK 10.0.22621:

    MSBuild OpenCBP_FO4.sln /t:CBPSSE /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.22621.0

The DLL is `x64\Release\cbp.dll`. The release build links the C runtime statically.

## Licence

- **The OpenCBP_FO4 / OCBPC code this fork starts from** (commit `abc0192` and before) is under
  the MIT licence: see `LICENSE`. Upstream moved its `cbpc` branch to the GPL-3.0 on 2021-07-21,
  after that commit.
- **fo4-anatomy's changes** are under the GNU General Public License, version 3: see `COPYING`.
- So `cbp.dll`, built from this repository, is distributed under the GPL-3.0. The MIT notice is
  kept for the OCBPC code it carries.
- **Additional permission under GNU GPL version 3 section 7.** If you modify this Program, or any
  covered work, by linking or combining it with F4SE (the Fallout 4 Script Extender, including its
  common libraries), containing parts covered by the terms of F4SE's licence, the licensors of this
  Program grant you additional permission to convey the resulting work.
- **Third-party code in the tree:**
  - `f4se/` is F4SE's source, by the F4SE team, for building plugins. Its readme requires a
    plugin's source to be public, which is one reason this repository is. It carries a two-line
    build fix for VS2022 in `f4se/f4se/PapyrusObjectReference.cpp`.
  - `common/` is Ian Patterson's common library, under the zlib-style terms in
    `common/common_license.txt`.
  - `detourxs-master/` is DetourXS, under the WTFPL. Its LDE64 length engine is BeaEngine's.
  - `f4se/xbyak/` is xbyak, under BSD-3-Clause.

## Credits

JS and the OpenCBP authors; ericncream, for OCBPC; the F4SE team; Ian Patterson, for `common`;
the DetourXS and BeaEngine authors. maximusmaxy's Screen Archer Menu source documented the face
data the mouth uses.
