// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: the penis finds the opening it is meant for (ocbp.ini [Aim], A-28).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// Animations are made against someone else's anatomy, so a shaft often misses by a little: beside
// the opening, or through her thigh. Each frame, before the colliders are built, UpdateAims turns
// every penis chain ([Aim] chain, root first) about its root onto the opening it is closest to entering
// -- a vagina or anus our bones mark (A-14), or any mouth ([Mouth]) -- and stretches it a little when
// it falls short. The correction goes on top of the animation's pose, eases in and out, and gives up
// past its angles (AimSolve.h), so a pose that is far off stays the animation's.
//
// Only people in a scene: Anatomy:Arousal reports AAF's busy actors every tick through the Papyrus
// native AnatomyAim.SetBusy (Scripts/AnatomyAim.pex). Out of a scene the bind pose points the bones
// forward, and two people standing close must never be aimed.
#include "INIReader.h"

class VirtualMachine;

void LoadAimConfig(INIReader& reader);
void UpdateAims();                         // scan.cpp: each frame, before the colliders are built
void ResetAims();                          // a save is loading: nothing we wrote is on the new skeletons
bool RegisterAimFuncs(VirtualMachine* vm);
bool AimSeesScene(unsigned int formID);    // in an AAF scene, per Anatomy:Arousal's last report (the probe)
