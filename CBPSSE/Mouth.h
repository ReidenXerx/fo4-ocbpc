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

void LoadMouthConfig(INIReader& reader);
void InstallMouthHook();
void UpdateMouths();
