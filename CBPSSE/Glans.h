// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-25: the glans as the mouth sees it (A-32).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// The contact mouth sees a penis as its collider spheres, the radius running straight from one bone's
// sphere to the next. That is right along the shaft and wrong at the head: the mushroom's crown (A-31)
// is not on the tip bone but behind it, where Penis_04's thin sphere and Penis_05's run a straight line
// under it. Measured on BodyTalk4's erect penis with the fork's shape (fo4-anatomy tools/glans_profile.py):
// at head x1.4 the crown is 2.52 at 1.84 behind Penis_05, and the spheres gave 1.75 there. The lips
// opened for a head 0.76 thinner than the one in her mouth, and its rim showed past both corners (the
// owner's Photo212, 2026-09-25).
//
// So a chain whose last bone carries the glans gets that bone's sphere replaced by the glans's own profile
// along the last segment. Each step is (along, radius) in units of the tip sphere's radius R, which scales
// with the bone (the shape's head size, the actor's scale): along < 0 behind the bone, and the last step is
// the bone itself (along 0), whose radius the tip's cap keeps. The profile is the same in R for every head
// size the shape makes (1.2-1.4 measured). The radius is the flesh's; `skin` is added back, since the mouth
// takes it off every collider sphere (a sphere sits on the shaft's ridge, the glans has none).
#include <cmath>
#include <vector>

namespace Glans
{
	struct Step
	{
		float along;    // x R along the last segment, from the tip bone: < 0 behind it
		float radius;   // x R: the flesh's radius there
	};

	// A profile is usable when its steps run strictly forward and end on the bone itself (along 0)
	inline bool Valid(const std::vector<Step>& profile)
	{
		if (profile.empty() || profile.back().along != 0.0f)
			return false;
		for (size_t i = 0; i < profile.size(); i++) {
			if (!(profile[i].radius > 0.0f) || profile[i].along > 0.0f)
				return false;
			if (i > 0 && !(profile[i].along > profile[i - 1].along))
				return false;
		}
		return true;
	}

	// pts: the chain's points, base to tip (each with .pos and .r); the last is the glans bone's sphere.
	// Steps that would fall behind the joint before it are not this bone's flesh and are skipped.
	template <class Point>
	void Shape(std::vector<Point>& pts, const std::vector<Step>& profile, float skin)
	{
		if (pts.size() < 2 || !Valid(profile))
			return;
		const Point tip = pts.back();
		const Point prev = pts[pts.size() - 2];
		auto d = tip.pos - prev.pos;
		float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		float R = tip.r;
		if (len < 1e-4f || !(R > 0.0f))
			return;
		d = d * (1.0f / len);
		pts.pop_back();
		for (const Step& s : profile) {
			float at = s.along * R;
			if (at <= -len)
				continue;
			Point p = tip;
			p.pos = tip.pos + d * at;
			p.r = s.radius * R + skin;
			pts.push_back(p);
		}
	}
}
