// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16: fluid strands between two contact points.
// The design is OStrands' (https://gitlab.com/LilRicefield/ostrands, GPL-3, by LilRicefield): a skinned tube with
// its own bones (meshes\Anatomy\Strand\Strand.nif, fo4-anatomy tools/strand_mesh.py), loaded once through
// BSModelDB::Demand, attached under an actor's 3D, its bones written every frame.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// Strands between a shaft and the opening Aim locked it in (vagina, anus or mouth), as OStrands makes them: on every
// outstroke 1-2 "bridges" from points on her rim to his shaft where it crosses her entrance, each end fixed in its node
// (her Pelvis_skin or head, his shaft bone) so they stretch as he pulls back; and on the pull-out, one from her entrance
// to his tip ([Strands] pulloutChance). Each is a rope of kBones points under gravity with viscosity, its volume kept so
// it thins as it stretches; it snaps in the middle past maxLength beyond its first length, under minRadius or after life
// seconds, and the halves thin away. Up to kPool at once: one mesh copy each (Strand00..07.nif, one Demand prototype
// apiece, since the game's clone is not bound). Without the meshes, nothing is loaded or touched.

#include "INIReader.h"
#include "Game.h"

namespace Strands
{
	void LoadConfig(INIReader& reader);   // ocbp.ini [Strands]
	void Tick();                          // once per frame from the scan, after UpdateAims
	void Reset();                         // a load or a new game: the old 3D is gone, so is our hold on its nodes
	// a climax (AAF's CLIMAX-tagged stage; Anatomy:Arousal -> AnatomyAim.Climax, Papyrus' thread): the stage's actors.
	// Each with a shaft chain: inside an opening Aim holds, drips from it once he is out; outside, shots from his tip
	void Climax(const std::vector<unsigned int>& a_actors);
}
