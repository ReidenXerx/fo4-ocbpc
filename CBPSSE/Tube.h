// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-26: a penis as one continuous tube for OCBPC's collisions.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// OCBPC collides sphere against sphere and ADDS every overlapping pair's push (Collision.cpp
// IsItColliding). A penis is five balls on its bones, ~3 apart, nothing between them. Measured
// against BodyTalk4's erect mesh with A-31's shape (fo4-anatomy tools/tube_check.py, 2026-09-26):
//   - along the shaft the surface a labia sphere rests on swings from 0.13 inside the flesh to 0.69
//     off it as the balls pass (the anus's smaller spheres 0.27 .. 0.69): a lip rides in and out;
//   - the tip ball stands 0.6 .. 1.4 ahead of the glans, so the opening parts before the head arrives;
//   - two balls touching one lip push it twice (and packing them closer only pushes harder).
// In fit_check's OCBPC port the vagina showed 592 of 1423 vertices through the shaft and the anus 260
// of 361; the same body against this tube, 435 and 153-173, and the anus's thrust wobble 0.69 -> 0.24.
//
// This is the mouth's lesson (A-32) applied to physics: the contact mouth reads a chain as a line whose
// radius runs straight from one bone's sphere to the next, with the glans's own profile (Glans.h); here
// a sphere pushed by such a chain gets ONE push, out of the tube at the point nearest to it, by exactly
// how far it overlaps. Free of F4SE: tests/tube runs it.
#include <cmath>
#include <vector>

namespace Tube
{
	// The deepest push a sphere (centre c, radius rs) gets out of a tube along pts (each with .pos and
	// .r, the tube's radius there): per segment, the nearest point with the radius blended along it;
	// false when the sphere does not reach the tube. out is the displacement for the sphere's centre.
	template <class Point, class Vec>
	bool Push(const std::vector<Point>& pts, const Vec& c, float rs, Vec& out)
	{
		bool hit = false;
		float best = 0.0f;
		auto consider = [&](const Vec& at, float r) {
			Vec d = c - at;
			float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
			float depth = r + rs - dist;
			if (depth <= 0.0f || depth <= best)
				return;
			best = depth;
			hit = true;
			if (dist > 1e-5f)
				out = d * (depth / dist);
			else
				out = Vec{ 0.0f, 0.0f, depth };       // dead on the line: any way out, one way
		};
		if (pts.size() == 1)
			consider(pts[0].pos, pts[0].r);
		for (size_t k = 0; k + 1 < pts.size(); k++) {
			const Vec a = pts[k].pos, b = pts[k + 1].pos;
			Vec ab = b - a, ac = c - a;
			float len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
			float s = len2 > 1e-8f ? (ac.x * ab.x + ac.y * ab.y + ac.z * ab.z) / len2 : 0.0f;
			s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
			consider(a + ab * s, pts[k].r + (pts[k + 1].r - pts[k].r) * s);
		}
		return hit;
	}
}
