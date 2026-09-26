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

// A-44: every actor whose 3D is loaded, not only the player's cell's. OCBPC's scan walks the object list
// of the cell the player stands in, so in the open world a woman one cell over was never pointed at our
// nodes: her genitals rode the stranded copies off the actor's root, near enough while she stood, left
// where she died once she ragdolled, and floating there when her body was re-equipped as she was looted
// (a player's report, 2026-09-26). The game's own TESObjectLoadedEvent (F4SE's 1.10.163 dispatcher)
// names every reference whose 3D loads; the actors among them are kept and visited a few per frame.
void WatchLoadedActors();                      // once, at kMessage_GameDataReady
// A-45 (the owner's reading of the same report): OCBPC simulates the player's cell only, and an actor that
// leaves it keeps whatever offset physics last wrote into our bones, up to [Labia] maxoffset 20 units,
// swung hardest by a death's ragdoll: the genitals stay stretched until the player comes close. So an
// actor not simulated this frame has our bones put back where [Bones] rests them (identity rotation).
// `simulated` holds the form ids OCBPC updates this frame (actorEntries).
void EnsureLoadedActors(int budget, const std::vector<UInt32>& simulated);
