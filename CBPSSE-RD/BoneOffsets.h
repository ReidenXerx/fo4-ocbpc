// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-11-23: per-actor bone translation offsets after the animation.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// A character whose own proportions differ from the FO4 skeleton (Ilham, General-mods 2026-11-23: her torso is 28%
// shorter) keeps them in game: her mesh is bound at HER joints, and each frame, after the animation posed the skeleton,
// her bones' local translations are scaled or offset to those joints. Animations still drive every rotation.
//
// One drop-in ini per mod: Data\F4SE\Plugins\AnatomyBoneOffsets\*.ini
//   [Actor]
//   plugin=Ilham.esp
//   base=0x000800              ; her NPC_ base form, local to the plugin
//   [Offsets]
//   SPINE1=*0.78               ; the animated local translation scaled
//   LArm_Collarbone=0,-1.2,0   ; added, in the parent's local axes (game units)
//   COM=0,0,-3.1
// Several [Actor] blocks are not supported: one actor per file.

void LoadBoneOffsets();      // reads the folder (config load and reload)
void UpdateBoneOffsets();    // every frame, after the animation, before the aim, colliders and physics
