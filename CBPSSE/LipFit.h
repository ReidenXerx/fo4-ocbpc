// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-25: the lips around what is in her mouth (A-32).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once

// The owner's look (2026-09-25): "minor clipping on mouth lips ... are we able to make mouth to automatically
// adjust to shape of thing (penis/hand) is puting there? like real humans lips do ... they shrink around it".
//
// A lip's inner edge (the rim of the head's mouth hole) moves by each mouth morph a measured amount at
// each point across the mouth (fo4-anatomy tools/lips.py, [Mouth] lip<sex><id>). Jaw Open opens the whole
// mouth; the funnels, Upper Lip Up / Down and Lower Lip Down / Up open or close it in the middle or on one
// side. Fit is the weights that put the upper edge just over what crosses her lips and the lower edge just
// under it, everywhere across the mouth that it spans, and keep the lips closed where it does not: the
// lips rest ON the shaft instead of a hole opened round it. Never inside it: being inside costs far more
// than a gap. Pure, and run in tests/lips.
namespace LipFit
{
	constexpr int kSamples = 7;           // points across the mouth ([Mouth] lipXs)
	constexpr int kMaxMorphs = 12;

	struct Table
	{
		int count = 0;
		int id[kMaxMorphs] = {};                          // the engine's morph id
		float up[kMaxMorphs][kSamples] = {};              // the upper edge's move at 1.0, at each sample
		float lo[kMaxMorphs][kSamples] = {};              // the lower edge's
	};

	// What crosses her lips, at each sample: its top and bottom relative to the resting lip line (head
	// units, up positive), or nothing there.
	struct Want
	{
		bool spans[kSamples] = {};
		float top[kSamples] = {};
		float bottom[kSamples] = {};
	};

	struct Params
	{
		float clearance = 0.05f;      // the lips rest this far off the surface
		float inside = 30.0f;         // the cost of an edge inside the surface, over a gap's 1
		float closed = 0.3f;          // the pull back to closed where nothing crosses
		float prefer = 0.02f;         // the cost of any weight: the fewest morphs that fit
		int steps = 200;              // sweeps at most (it stops once nothing moves)
	};

	// Weights in [0, 1] per table morph; start (count entries, or null) is last frame's, for a steady fit.
	void Fit(const Table& t, const Want& want, const Params& p, const float* start, float* out);

	// The edges these weights give, at each sample (for tests and the log).
	void Edges(const Table& t, const float* w, float* upper, float* lower);
}
