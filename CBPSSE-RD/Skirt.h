// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-01: skirt bones (roadmap 5).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// fo4-anatomy (roadmap 5): legs that push hanging cloth instead of passing through it.
//
// Anatomy Tailor weights a coat's or dress's hanging cloth to a ring of skirt nodes ([Skirt] columns x levels, named
// <prefix><column 00..>_<level>), which [Bones] / [BonesMale] create under Pelvis_skin like our genital nodes, only on
// an actor whose skin names them. Each frame this moves them: a damped swing toward their rest in Pelvis_skin's frame,
// each column keeping its length both ways, neighbours not tearing apart, and out of the legs' capsules (thigh and
// calf, from the skeleton's own leg nodes) -- a leg that LANDED on a node (an AAF scene snapped into its pose) sends it
// to its column's side, and a column never passes through a leg. The same solver, step for step, is fo4-anatomy's
// tools/skirt.py, where it was measured (fo4-refit studies/legs_skirt.py).
#include "INIReader.h"

#include "Game.h"
void LoadSkirtConfig(INIReader& reader);
void UpdateSkirt(Actor* actor);   // per frame, per near actor; nothing when it has no skirt nodes
void ResetSkirt();                // a load or a new game: no state crosses into it
