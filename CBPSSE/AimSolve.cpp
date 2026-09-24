// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24 (A-28). What each part does: AimSolve.h.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "AimSolve.h"

#include <algorithm>
#include <cmath>

namespace AimSolve
{
	V3 Add(const V3& a, const V3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	V3 Sub(const V3& a, const V3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	V3 Scale(const V3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	V3 Cross(const V3& a, const V3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	float Length(const V3& a) { return std::sqrt(Dot(a, a)); }
	V3 Normalized(const V3& a)
	{
		float l = Length(a);
		return l > 1e-6f ? Scale(a, 1.0f / l) : V3{};
	}

	static Quat Norm(Quat q)
	{
		float l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
		if (l < 1e-9f)
			return Quat{};
		return { q.w / l, q.x / l, q.y / l, q.z / l };
	}

	Quat Mul(const Quat& a, const Quat& b)
	{
		return { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
		         a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		         a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		         a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
	}

	Quat Conj(const Quat& q) { return { q.w, -q.x, -q.y, -q.z }; }

	V3 Rotate(const Quat& q, const V3& v)
	{
		Quat p{ 0.0f, v.x, v.y, v.z };
		Quat r = Mul(Mul(q, p), Conj(q));
		return { r.x, r.y, r.z };
	}

	Quat FromTo(const V3& from, const V3& to)
	{
		V3 a = Normalized(from), b = Normalized(to);
		float d = Dot(a, b);
		if (d > 0.999999f)
			return Quat{};
		if (d < -0.999999f) {                       // opposite: any axis across a
			V3 axis = Cross(a, V3{ 1, 0, 0 });
			if (Length(axis) < 1e-3f)
				axis = Cross(a, V3{ 0, 1, 0 });
			axis = Normalized(axis);
			return { 0.0f, axis.x, axis.y, axis.z };
		}
		V3 c = Cross(a, b);
		return Norm(Quat{ 1.0f + d, c.x, c.y, c.z });
	}

	Quat Slerp(Quat a, const Quat& b, float t)
	{
		float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
		if (d < 0.0f) {                              // the short way round
			a = { -a.w, -a.x, -a.y, -a.z };
			d = -d;
		}
		if (d > 0.9995f)
			return Norm({ a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t });
		float th = std::acos(d);
		float s = std::sin(th);
		float wa = std::sin((1.0f - t) * th) / s, wb = std::sin(t * th) / s;
		return Norm({ a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb });
	}

	float Angle(const Quat& q)
	{
		float w = std::fabs(q.w);
		return 2.0f * std::acos((std::min)(1.0f, w));
	}

	M3 ToMatrix(const Quat& q0)
	{
		Quat q = Norm(q0);
		M3 r;
		r.m[0][0] = 1 - 2 * (q.y * q.y + q.z * q.z);
		r.m[0][1] = 2 * (q.x * q.y - q.z * q.w);
		r.m[0][2] = 2 * (q.x * q.z + q.y * q.w);
		r.m[1][0] = 2 * (q.x * q.y + q.z * q.w);
		r.m[1][1] = 1 - 2 * (q.x * q.x + q.z * q.z);
		r.m[1][2] = 2 * (q.y * q.z - q.x * q.w);
		r.m[2][0] = 2 * (q.x * q.z - q.y * q.w);
		r.m[2][1] = 2 * (q.y * q.z + q.x * q.w);
		r.m[2][2] = 1 - 2 * (q.x * q.x + q.y * q.y);
		return r;
	}

	Quat FromMatrix(const M3& r)
	{
		const auto& m = r.m;
		float tr = m[0][0] + m[1][1] + m[2][2];
		Quat q;
		if (tr > 0.0f) {
			float s = std::sqrt(tr + 1.0f) * 2.0f;
			q = { 0.25f * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s };
		}
		else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
			float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
			q = { (m[2][1] - m[1][2]) / s, 0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s };
		}
		else if (m[1][1] > m[2][2]) {
			float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
			q = { (m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s };
		}
		else {
			float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
			q = { (m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s };
		}
		return Norm(q);
	}

	const char* KindName(int kind)
	{
		switch (kind) {
		case kVagina: return "vagina";
		case kAnus: return "anus";
		case kMouth: return "mouth";
		default: return "?";
		}
	}

	Fit Judge(const Chain& c, const Target& t, const Params& p, bool keep)
	{
		Fit f;
		if (t.owner == c.owner || c.length <= 0.0f)
			return f;                                // never one's own opening
		if (p.requireScene && !(c.inScene && t.inScene))
			return f;
		V3 shaft = Sub(c.tip, c.base);
		if (Length(shaft) < 1e-4f)
			return f;
		V3 u = Normalized(shaft);
		float entrance = Length(Sub(t.point, c.base));
		if (entrance < p.minReach || entrance > c.length * p.reach)
			return f;
		V3 aim = Add(t.point, Scale(t.in, p.depth));
		V3 d = Normalized(Sub(aim, c.base));
		// (an opening behind the root needs a turn far past keepAngle: the angle below refuses it)
		if (Dot(d, t.in) < std::cos(p.entryAngle))
			return f;                                // from the side, or from inside
		float angle = std::acos((std::max)(-1.0f, (std::min)(1.0f, Dot(u, d))));
		if (angle > (keep ? p.keepAngle : p.captureAngle))
			return f;
		f.ok = true;
		f.angle = angle;
		f.world = FromTo(u, d);
		f.stretch = (std::max)(1.0f, (std::min)(p.maxStretch, (entrance + p.minInside) / c.length));
		return f;
	}

	Result Update(State& s, const Chain& c, const std::vector<Target>& targets, const Params& p, float dt)
	{
		Result r;
		Fit best;
		const Target* chosen = nullptr;
		// a lock held stays while it still fits, loosely
		if (s.locked) {
			for (auto& t : targets) {
				if (t.owner == s.lockedOwner && t.kind == s.lockedKind) {
					Fit f = Judge(c, t, p, true);
					if (f.ok) {
						best = f;
						chosen = &t;
					}
					break;
				}
			}
		}
		// otherwise the opening that needs the smallest turn
		if (!chosen) {
			for (auto& t : targets) {
				Fit f = Judge(c, t, p, false);
				if (f.ok && (!chosen || f.angle < best.angle)) {
					best = f;
					chosen = &t;
				}
			}
			r.newLock = chosen != nullptr;
		}
		s.locked = chosen != nullptr;
		s.lockedOwner = chosen ? chosen->owner : 0;
		s.lockedKind = chosen ? chosen->kind : -1;

		// the turn wanted, in the parent's frame: world q = P l P^-1, so l = P^-1 q P
		Quat want = chosen ? Mul(Mul(Conj(c.parent), best.world), c.parent) : Quat{};
		float wantStretch = chosen ? best.stretch : 1.0f;
		float a = dt > 0.0f ? 1.0f - std::exp(-p.rate * dt) : 0.0f;
		s.correction = Slerp(s.correction, want, a);
		s.stretch += (wantStretch - s.stretch) * a;

		r.local = s.correction;
		r.stretch = s.stretch;
		r.locked = s.locked;
		r.targetOwner = s.lockedOwner;
		r.targetKind = s.lockedKind;
		r.angle = chosen ? best.angle : 0.0f;
		r.active = chosen || Angle(s.correction) >= 1e-4f || std::fabs(s.stretch - 1.0f) >= 1e-4f;
		return r;
	}
}
