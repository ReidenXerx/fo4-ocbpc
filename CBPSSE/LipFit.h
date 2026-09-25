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
	constexpr int kMaxMorphs = 16;

	struct Table
	{
		int count = 0;
		int id[kMaxMorphs] = {};                          // the engine's morph id
		float up[kMaxMorphs][kSamples] = {};              // the upper edge's move at 1.0, at each sample
		float lo[kMaxMorphs][kSamples] = {};              // the lower edge's
		// ACROSS (the owner, 2026-09-25: "fit head of penis IN HORIZONTAL AXIS"): the mouth's inner
		// corners, the rim's ends at rest (left < 0 < right; both 0 = not measured, no across terms), and
		// how far each morph moves them at 1.0 (Jaw Open widens, the funnels narrow, Corner Out opens)
		float restLeft = 0.0f, restRight = 0.0f;
		float left[kMaxMorphs] = {}, right[kMaxMorphs] = {};
		// THE CORNERS (the owner, 2026-09-26: "the corner of mouth still kinda static"): the rim's ends are its
		// extremes, not where the lips meet. That vertex: Jaw Open takes it IN (0.26 / 0.18 on the female
		// head) while the opening's widest point goes out, and Lip Corner In (7 / 30) takes it in 0.32 / 0.38,
		// which the extremes never showed. Rest positions (both 0 = not measured: no hug) and each morph's move.
		float restCornerL = 0.0f, restCornerR = 0.0f;
		float cornerL[kMaxMorphs] = {}, cornerR[kMaxMorphs] = {};
	};

	// What crosses her lips, at each sample: its top and bottom relative to the resting lip line (head
	// units, up positive), or nothing there.
	struct Want
	{
		bool spans[kSamples] = {};
		float top[kSamples] = {};
		float bottom[kSamples] = {};
		bool across = false;          // its extent across the mouth (lo < hi), which the corners must clear
		float lo = 0.0f, hi = 0.0f;
	};

	struct Params
	{
		float clearance = 0.05f;      // the lips rest this far off the surface
		float inside = 30.0f;         // the cost of an edge inside the surface, over a gap's 1
		float closed = 0.3f;          // the pull back to closed where nothing crosses
		float prefer = 0.02f;         // the cost of any weight: the fewest morphs that fit
		float hug = 2.0f;             // where the lips meet is drawn to the section's sides (plus clearance),
		                              // either way, at this over a gap's 1: in on a thin shaft as the lips close
		                              // on top and below, out with the head where it widens
		int steps = 200;              // sweeps at most (it stops once nothing moves)
	};

	// Weights in [0, 1] per table morph; start (count entries, or null) is last frame's, for a steady fit.
	void Fit(const Table& t, const Want& want, const Params& p, const float* start, float* out);

	// The edges these weights give, at each sample (for tests and the log).
	void Edges(const Table& t, const float* w, float* upper, float* lower);

	// The corners these weights give (the rim's left and right end).
	void Ends(const Table& t, const float* w, float& left, float& right);

	// Where the lips meet, left and right, with these weights (the hug's corners).
	void Corners(const Table& t, const float* w, float& left, float& right);
}
