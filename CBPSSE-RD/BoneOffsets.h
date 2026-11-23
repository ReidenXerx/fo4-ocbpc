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
//   [Rotations]                ; the animated local rotation turned, in degrees, about the bone's OWN axes:
//   LArm_UpperArm=0,0,-8       ; X first, then the turned Y, then the twice-turned Z; right-handed (+X turns Y toward Z)
// A bone may be in both sections. Its joint stays where it is; everything below it swings.
//   [Log]                      ; optional: the bones' positions after the offsets, one log line per sample
//   bones=COM,SPINE2,Chest,LArm_UpperArm
//   every=1.0                  ; seconds between samples
//   samples=30
// Each sample gives every bone in the skeleton's own space (the frame of a nif's bind pose) and in the world, plus
// the root's position and heading. A file with [Log] and no [Offsets] samples the plain pose (the A/B baseline).
// Several [Actor] blocks are not supported: one actor per file.

void LoadBoneOffsets();      // reads the folder (config load and reload)
void UpdateBoneOffsets();    // every frame, after the animation, before the aim, colliders and physics
