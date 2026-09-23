// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-23: our genital bones, added to the loaded skeleton at run time (ocbp.ini [Bones]).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// fo4-anatomy: our genital bones, added at run time to whatever skeleton the actor loaded (A-21).
//
// The body is weighted to bones no skeleton ships (AnatVulva, the lips, the anus and their
// _Stretch children). A skinned mesh whose bone the skeleton lacks keeps pointing at its own copy
// of that node, which never moves with the pelvis: the "fin". Instead of shipping a patched
// skeleton (a file that must win over every skeleton mod), this finds the body's skin instance,
// takes the skeleton's real Pelvis_skin from it, creates any of our nodes that are missing under
// it (ocbp.ini [Bones]: name=parent,x,y,z, the local offset, identity rotation), and points the
// skin's entries for our names at them (BSSkin::Instance: bones +0x10, worldTransforms +0x28).
// OCBPC's per-frame UpdateConfig then binds the new nodes by name like any other bone.
#include "INIReader.h"
#include "f4se/GameReferences.h"

void LoadBonesConfig(INIReader& reader);
// true when nodes were created for this actor this frame
bool EnsureAnatomyBones(Actor* actor);
