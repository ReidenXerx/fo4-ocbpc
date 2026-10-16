// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16: the climax fluid (ROADMAP item 6).
// The design is OStrands' FluidManager (https://gitlab.com/LilRicefield/ostrands, GPL-3, by LilRicefield).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// A climax (Strands.cpp hands it over once our meshes load) becomes particles (FluidSim.h) in a domain around the
// people of the scene: pulses from his tip when he is outside her, each a stream that breaks into drops; when he is
// inside an opening (vagina, anus or mouth), most of it goes into a reservoir for that opening and only a share
// backs out around his shaft. The reservoir fills up to its capacity (more spills while he is in); when he pulls out
// it gushes, then seeps out over the next minute or so, faster when she stands or moves, slower when the opening faces
// up; a mouth swallows it or lets it run out. The particles stick to both bodies (their collision spheres and his
// shaft), run down under gravity, gather, drip and pool on the floor (the plane under the lowest one's feet).
// They are drawn as OStrands draws them: crowded particles as blobs (a sphere with a bone per vertex,
// meshes\Anatomy\Fluid\Blob00..47.nif) wrapped around them, linked runs as ropes (the strand tube,
// meshes\Anatomy\Fluid\Rope00..31.nif), lone ones as drops. Without the meshes nothing loads and Strands keeps its
// own shots and drips.

#include "INIReader.h"
#include "Game.h"

namespace Fluid
{
	void LoadConfig(INIReader& reader);   // ocbp.ini [Fluid]
	void Tick();                          // once per frame from the scan, after Strands::Tick
	void Reset();                         // a load or a new game
	bool Ready();                         // [Fluid] on and the meshes load: Strands hands its climaxes over
	// a climax: him, whoever is with him, and the opening he is in (her, 0 vagina 1 anus 2 mouth), or her = 0: outside
	void Climax(unsigned int him, unsigned int other, unsigned int her, int kind);
}
