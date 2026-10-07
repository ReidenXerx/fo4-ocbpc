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

#include "Game.h"

void LoadAimConfig(INIReader& reader);
void UpdateAims();                         // scan.cpp: each frame, before the colliders are built
void ResetAims();                          // a save is loading: nothing we wrote is on the new skeletons
bool RegisterAimFuncs(RE::BSScript::IVirtualMachine* vm);
bool AimSeesScene(unsigned int formID);    // in an AAF scene, per Anatomy:Arousal's last report (the probe)
// How deep a locked shaft is this frame, in units past the entrance (0: none): for her, a shaft in her
// vagina or anus (a mouth's is the contact mouth's own); for him, his own in whatever he is locked on
// (a mouth measured from her lips). For the deep face (A-29), both actors. UpdateAims fills it.
float AimDepth(unsigned int formID);
unsigned int AimPartner(unsigned int formID);   // who that depth is with this frame (0: none), A-67
// A-67's oral sounds: a shaft in THIS actor's mouth (units past the mouth's entrance, 0: none) and whose it is;
// and whether this actor's own shaft is locked in a mouth (his AimDepth is then an oral one)
float AimOralDepth(unsigned int formID);
unsigned int AimOralPartner(unsigned int formID);
bool AimInMouth(unsigned int formID);
bool AimReceived(unsigned int formID);
// A-67's contacts: which opening AimDepth is in (0 vagina, 1 anus, 2 mouth; -1 none); a shaft through a gripping
// hand (a handjob: the tip past the grip, units) and whose hand; and this frame's openings (entrance and inward axis)
unsigned AimReceivedKinds(unsigned int formID);   // which of her openings hold a shaft: bit 0 vagina, bit 1 anus (a double penetration: both)
int AimDepthKind(unsigned int formID);
float AimGripDepth(unsigned int formID);
unsigned int AimGripPartner(unsigned int formID);
struct AimOpening
{
	unsigned int owner;
	int kind;
	NiPoint3 point;
	NiPoint3 in;
	bool inScene;
};
const std::vector<AimOpening>& AimOpenings();   // this actor's vagina or anus holds a shaft (its AimDepth is a received one)
// the shaft locked in this woman's opening (kind 0 vagina, 1 anus) this frame: its chain's world points, root to tip
bool AimShaft(unsigned int owner, int kind, std::vector<NiPoint3>& joints);
// The health check (Health.cpp): the actor's chain as UpdateAims would find it (-1 none configured, 0 missing,
// 1 not hanging one from the next, 2 aimed, 3 aimed with nodes between); [Aim] enabled; "first ... last".
int AimChainState(Actor* a);
bool AimOn();
std::string AimChainText();
