// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16: fluid strands between two contact points.
// The design is OStrands' (https://gitlab.com/LilRicefield/ostrands, GPL-3, by LilRicefield): a skinned tube with
// its own bones (meshes\Anatomy\Strand\Strand.nif, fo4-anatomy tools/strand_mesh.py), loaded once through
// BSModelDB::Demand, attached under an actor's 3D, its bones written every frame.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// When a shaft leaves an opening in a scene (Aim's vagina, anus or mouth), a strand may stay between her entrance and
// his tip ([Strands] chance): a rope of kBones points under gravity, sagging, thinning in the middle as the two part,
// broken past maxLength, thinned away after life seconds or when the shaft goes back in. One at a time: the mesh is
// one Demand prototype (more need OStrands' Clone()). Without the mesh, nothing is loaded or touched.

#include "INIReader.h"
#include "Game.h"

namespace Strands
{
	void LoadConfig(INIReader& reader);   // ocbp.ini [Strands]
	void Tick();                          // once per frame from the scan, after UpdateAims
	void Reset();                         // a load or a new game: the old 3D is gone, so is our hold on its nodes
}
