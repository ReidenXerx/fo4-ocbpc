# fo4-ocbpc

OCBPC (OpenCBP physics with collisions, for Fallout 4) as extended by **fo4-anatomy**. This is the
source of the `cbp.dll` that ships with *Anatomy - CBBE Genitals, Physics and Arousal*.

It is a fork of [ericncream/OpenCBP_FO4](https://github.com/ericncream/OpenCBP_FO4) at commit
`abc0192` (2020-04-13), the code behind the OCBPC 0.3 release. That in turn derives from OpenCBP
by JS.

Game: Fallout 4 1.10.163 (Steam and GOG) with F4SE 0.6.23. The mouth reads the engine code it
hooks before touching it, and stays off on any other build.

## What fo4-anatomy adds

- **Stretch groups.** A bone can follow the spread of a group of collider contacts, so an opening
  widens for something bigger instead of letting it clip through.
- **Prop colliders** (`[Props]`): whatever an animation hangs on a hand node (a toy, a bat)
  collides along its length. `targets=` limits which bones a prop may push. fo4-anatomy lists its
  genital and anus bones, so a mug held at the chest pushes nothing.
- **Run-time bones** (`[Bones]`): named nodes are added under each skeleton's `Pelvis_skin` when a
  body is skinned to them. No skeleton file has to be patched or shipped.
- **fo4-anatomy's own config files**: `Data\F4SE\Plugins\Anatomy\ocbp.ini` and
  `OCBPCollisionConfig.txt` are read after the player's own. Sections come per file, and collision
  spheres of a node both list are appended. Nothing of the player's physics mod is overwritten.
- **The contact-driven mouth** (`[Mouth]`): while a penis is at a woman's lips, Jaw Open, the lip
  funnels and Upper Lip Up are written over the face's merged expression weights. The merge's one
  call is repointed through F4SE's branch trampoline; the merge's own code is never modified.
- **A discovery log**, `Documents\My Games\Fallout4\F4SE\anatomy_ocbpc.log`, with the last four
  runs kept as `.1` to `.4`. It lists genital-looking nodes, props, run-time bones and mouths.
- **A fix in DetourXS**: on x64 it now asks its length engine for x64 decoding. The x86 decoding
  counted REX prefixes as instructions of their own, and could cut an instruction in two.

Each source file fo4-anatomy added or changed says so in its first lines, with the date and what
changed. `git log abc0192..` has the full history.

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
