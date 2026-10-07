// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-07: the vaginal canal wraps the shaft inside her (ocbp.ini [Canal]).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// The canal is a narrow tube (radius ~0.6) and the shaft that the aim (Aim.h) lays along her path is ~1.55 thick: the
// shaft showed through the walls. fo4-anatomy weights the canal's wall to rings of nodes ([Canal] rings x spokes, named
// <prefix><ring 1..>_<spoke 0..>, created under Pelvis_skin by [Bones] like our other nodes; not physics bones). Each
// frame, for a woman with a shaft in her vagina, every ring the tip has reached (easing in over `lead` units of its
// travel) moves its spokes onto a circle of `radius` around the shaft where it passes the ring; the rest of the time
// they ease back to where [Bones] rests them, at `rate` per second either way.
#include "INIReader.h"

#include "Game.h"
void LoadCanalConfig(INIReader& reader);
void UpdateCanal(Actor* actor);   // per frame, per near actor, after UpdateAims; nothing without our ring nodes
void ResetCanal();                // a load or a new game: no state crosses into it
