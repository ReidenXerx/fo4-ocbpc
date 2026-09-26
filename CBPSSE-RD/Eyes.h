// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-25: glances, one actor looking into another's eyes (RFAG).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// The owner (2026-09-25): "glance in the partner eyes. for ex during blowjob time to time glances on 1-2
// seconds maybe in another poses". Rapport decides when and who (RFAG, FaceAuthority.h); this plugin turns
// the eyes (Glance.h: the engine slides the eye texture) and opens the lids (FaceCompose layer 4).
//
// The engine's eye update is hooked at its two calls (it runs as it is, then each running glance writes its
// eye); a glance that ends is simply not written again, and the engine eases the eye back to its own target.
//
// ocbp.ini [Eyes]:
//   enabled=1   hook the eye update (0: no glance ever turns an eye; the lids still open)
//   glances=0   1: tell Rapport (hello bit 4) that glances work. Only once the owner has seen them work.
//   axes=a,b,c,d        u = 0.25 (a up + b side), v = 0.25 (c up + d side); default 0,1,1,0: the photo's
//                       map (2026-09-26: u sideways, u+ her right; v up and down, v+ up)
//   signUp / signSide   the first reading (u = -0.25 up, v = -0.25 side), used only with axes=0,0,0,0
//   eyeRise / eyeBack   the eyes, from the mouth's point: this far up the face and back into it
//   probe=0     1: log each actor's engine eye offset beside where the player's eyes are, twice a second (dev)
//   test=0      1: every actor looks into the nearest other actor's eyes 1.5 s of every 4 (dev)
#include "INIReader.h"

#include <vector>

struct ActorEntry;

void LoadEyeConfig(INIReader& reader);
void InstallEyeHook();                              // at the same point as the mouth's
bool EyesTurn();                                    // hooked and [Eyes] glances=1: the hello's bit 4
float EyeLidMax(unsigned int formID);               // for FaceCompose: 1 = no glance holds the lids
unsigned long long EyeClockMs();                    // the glances' clock
void UpdateEyeProbe(const std::vector<ActorEntry>& actors, float dt);   // UpdateMouths: [Eyes] probe / test
