// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-25: the lips around what is in her mouth (A-32).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "LipFit.h"

#include <algorithm>
#include <cmath>

namespace LipFit
{
	void Edges(const Table& t, const float* w, float* upper, float* lower)
	{
		for (int k = 0; k < kSamples; k++) {
			float u = 0.0f, l = 0.0f;
			for (int m = 0; m < t.count; m++) {
				u += w[m] * t.up[m][k];
				l += w[m] * t.lo[m][k];
			}
			upper[k] = u;
			lower[k] = l;
		}
	}

	void Ends(const Table& t, const float* w, float& left, float& right)
	{
		left = t.restLeft;
		right = t.restRight;
		for (int m = 0; m < t.count; m++) {
			left += w[m] * t.left[m];
			right += w[m] * t.right[m];
		}
	}

	// Cyclic coordinate descent on a piecewise quadratic, each weight in turn to its best in [0, 1] with
	// the others held: per sample, the upper edge toward top + clearance and the lower toward bottom -
	// clearance (an edge on the wrong side costs `inside` times a gap), or both toward closed. Eleven
	// weights converge in tens of sweeps; a plain gradient step, bounded by the inside cost, crawls.
	void Fit(const Table& t, const Want& want, const Params& p, const float* start, float* out)
	{
		const int n = (std::min)(t.count, kMaxMorphs);
		float w[kMaxMorphs] = {};
		for (int m = 0; m < n; m++)
			w[m] = start ? (std::max)(0.0f, (std::min)(1.0f, start[m])) : 0.0f;
		float u[kSamples], l[kSamples], el, er;
		Edges(t, w, u, l);
		Ends(t, w, el, er);
		// across: a corner INSIDE the section's extent costs like an edge inside it; wider costs nothing
		const bool across = want.across && t.restLeft < 0.0f && t.restRight > 0.0f;
		for (int sweep = 0; sweep < p.steps; sweep++) {
			float moved = 0.0f;
			for (int m = 0; m < n; m++) {
				float g = 2.0f * p.prefer * w[m], h = 2.0f * p.prefer;
				for (int k = 0; k < kSamples; k++) {
					float ru, rl, cu, cl;
					if (want.spans[k]) {
						ru = u[k] - (want.top[k] + p.clearance);          // < 0: the upper lip is inside
						rl = l[k] - (want.bottom[k] - p.clearance);       // > 0: the lower lip is inside
						cu = ru < 0.0f ? p.inside : 1.0f;
						cl = rl > 0.0f ? p.inside : 1.0f;
					}
					else {
						ru = u[k];
						rl = l[k];
						cu = cl = p.closed;
					}
					g += 2.0f * (cu * ru * t.up[m][k] + cl * rl * t.lo[m][k]);
					h += 2.0f * (cu * t.up[m][k] * t.up[m][k] + cl * t.lo[m][k] * t.lo[m][k]);
				}
				if (across) {
					// > 0: that corner is inside it, and costs `inside`; wider costs nothing. The fewest
					// morphs (prefer) then stop a corner right at its target. (A pull back for corners pushed
					// past it by the other side's Corner Out was tried: it bought ~0.06, less than it cost.)
					float tL = want.lo - p.clearance, tR = want.hi + p.clearance;
					float rL = el - tL, rR = tR - er;
					float cL = rL > 0.0f ? p.inside : 0.0f;
					float cR = rR > 0.0f ? p.inside : 0.0f;
					g += 2.0f * cL * rL * t.left[m] - 2.0f * cR * rR * t.right[m];
					h += 2.0f * (cL * t.left[m] * t.left[m] + cR * t.right[m] * t.right[m]);
				}
				if (h <= 0.0f)
					continue;
				float next = (std::max)(0.0f, (std::min)(1.0f, w[m] - g / h));
				float d = next - w[m];
				if (d == 0.0f)
					continue;
				w[m] = next;
				moved = (std::max)(moved, std::fabs(d));
				for (int k = 0; k < kSamples; k++) {
					u[k] += d * t.up[m][k];
					l[k] += d * t.lo[m][k];
				}
				el += d * t.left[m];
				er += d * t.right[m];
			}
			if (moved < 1e-5f)
				break;
		}
		for (int m = 0; m < n; m++)
			out[m] = w[m];
		for (int m = n; m < t.count && m < kMaxMorphs; m++)
			out[m] = 0.0f;
	}
}
