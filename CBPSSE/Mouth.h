// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-23: the contact-driven mouth, written over the face's merged morphs (ocbp.ini [Mouth]).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// fo4-anatomy: the mouth opens to what is in it (ocbp.ini [Mouth]).
//
// FO4 heads have no mouth bones: a face moves through 50 expression morphs, and the engine merges
// them every frame into the weights the face mesh is built from (BSFaceGenAnimationData + 0x18) as
// max(override + 0xF0, animation + 0x1C8), in the function at 0x6689D0 (1.10.163; its only caller is
// 0x6860FA, which rebuilds the mesh when it returns true). Screen Archer Menu's source gave the
// layout and the actor's pointer to it (MiddleProcess data + 0x3C8); the merge was read out of the
// executable.
//
// Each frame, UpdateMouths measures every penis chain and prop against every mouth: where one
// crosses the plane of her lips inside the mouth, the lower lip must drop below its bottom (Jaw
// Open), the upper lip lifts when it rides above the lip line, and the lips round a little. A hook
// on the merge then writes those values OVER the merged weights, so the animation's own mouth
// (and AAF's, and anyone's) gives way while something is there, and comes back when it is gone.
#include "INIReader.h"
#include "f4se/PluginAPI.h"

void LoadMouthConfig(INIReader& reader);
void InstallMouthHook();
void UpdateMouths();

// The mouth [Mouth] places on this actor's HEAD (the line where her lips meet), the way the face looks
// (out of the mouth) and, if asked, the head's up: what the aim (Aim.h, A-28) enters. False without a head.
class Actor;
class NiPoint3;
bool MouthOpening(Actor* actor, NiPoint3& centre, NiPoint3& outward, NiPoint3* up = nullptr);

// Rapport's face authority (fo4-anatomy A-27, FaceAuthority.h): the same hook writes the faces Rapport
// holds, under the mouth. main.cpp forwards F4SE's own messages here.
void LoadFaceConfig(INIReader& reader);         // ocbp.ini [Face]: authority, probe, test
void ListenForFaces(F4SEMessagingInterface* messaging, PluginHandle self);   // PostLoad: every plugin is loaded
void SayFaceHello();                            // PostPostLoad: Rapport listens by now
void ReleaseAllFaces(const char* why);          // PreLoadGame, PostLoadGame, NewGame
void RefreshHeldFaces();                        // a cell change: the frame OCBPC's scan skips
void StartFaceAuthorityTest();                  // PostLoadGame: [Face] test, if set
