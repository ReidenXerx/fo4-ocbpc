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
		case kHand: return "hand";
		default: return "?";
		}
	}

	void Pose(const Chain& c, const std::vector<Quat>& locals, float stretch, std::vector<V3>& joints,
		std::vector<Quat>& world)
	{
		size_t n = c.locals.size();
		joints.assign(n, V3{});
		world.assign(n, Quat{});
		if (!n)
			return;
		joints[0] = c.root;
		world[0] = Mul(c.parent, locals[0]);
		for (size_t i = 1; i < n; i++) {
			joints[i] = Add(joints[i - 1], Rotate(world[i - 1], Scale(c.offsets[i], stretch)));
			world[i] = Mul(world[i - 1], locals[i]);
		}
	}

	float ChainLength(const Chain& c)
	{
		float l = 0.0f;
		for (size_t i = 1; i < c.offsets.size(); i++)
			l += Length(c.offsets[i]);
		return l;
	}

	Fit Judge(const Chain& c, const std::vector<V3>& joints, const Target& t, const Params& p, bool keep)
	{
		Fit f;
		float length = ChainLength(c);
		bool hand = t.kind == kHand;
		if ((t.owner == c.owner && !hand) || length <= 0.0f || joints.size() < 2) {
			f.why = "their own";
			return f;                                // never one's own opening (one's own hand, yes)
		}
		if (p.requireScene && !(c.inScene && t.inScene)) {
			f.why = "not in a scene";
			return f;
		}
		V3 base = joints.front();
		V3 shaft = Sub(joints.back(), base);
		if (Length(shaft) < 1e-4f)
			return f;
		V3 u = Normalized(shaft);
		V3 q = Sub(t.point, base);
		float entrance = Length(q);
		if (entrance < p.minReach || entrance > length * p.reach) {
			f.why = entrance < p.minReach ? "too close to the root" : "out of reach";
			return f;
		}
		// how far the animation's shaft line passes from the entrance: a near miss is an entry the
		// animation meant; a hand job beside her is not
		float miss = Length(Sub(q, Scale(u, Dot(q, u))));
		if (miss > (keep ? p.keepMiss : p.captureMiss)) {
			f.why = "the animation's shaft passes too far from it";
			return f;
		}
		V3 aim = Add(t.point, Scale(t.in, p.depth));
		V3 d = Normalized(Sub(aim, base));
		// (an opening behind the root needs a turn far past keepAngle: the angle below refuses it)
		if (Dot(d, t.in) < std::cos(p.entryAngle)) {
			f.why = "from the side or from inside";
			return f;
		}
		float angle = std::acos((std::max)(-1.0f, (std::min)(1.0f, Dot(u, d))));
		if (angle > (keep ? p.keepAngle : p.captureAngle)) {
			f.why = "the turn it needs is too wide";
			return f;
		}
		f.ok = true;
		f.angle = angle;
		f.miss = miss;
		f.stretch = hand ? 1.0f : (std::max)(1.0f, (std::min)(p.maxStretch, (entrance + p.minInside) / length));
		return f;
	}

	Target Oriented(const Target& t, const std::vector<V3>& joints)
	{
		// a grip has no inside: it is entered from whichever side the shaft comes
		Target o = t;
		if (t.kind == kHand && joints.size() >= 2 && Dot(o.in, Sub(joints.back(), joints.front())) < 0.0f)
			o.in = Scale(o.in, -1.0f);
		return o;
	}

	static float SegmentDistance(const V3& x, const V3& a, const V3& b)
	{
		V3 ab = Sub(b, a);
		float l2 = Dot(ab, ab);
		float t = l2 > 1e-8f ? Dot(Sub(x, a), ab) / l2 : 0.0f;
		t = (std::max)(0.0f, (std::min)(1.0f, t));
		return Length(Sub(x, Add(a, Scale(ab, t))));
	}

	void ShapeFactors(size_t n, float shaft, float head, std::vector<float>& scaleMul, std::vector<float>& offsetMul)
	{
		scaleMul.assign(n, 1.0f);
		offsetMul.assign(n, 1.0f);
		if (n < 2 || shaft <= 0.0f || head <= 0.0f)
			return;
		if (n == 2) {
			scaleMul[1] = head;
			return;
		}
		scaleMul[1] = shaft;
		for (size_t k = 2; k < n; k++)
			offsetMul[k] = 1.0f / shaft;                  // under node 1's scale: the joint stays put
		scaleMul[n - 1] = head / shaft;                   // the tip inherits the shaft's; this makes it head
	}

	float HeadFor(std::uint32_t formID, float lo, float hi)
	{
		std::uint32_t h = formID * 2654435761u;           // Knuth's multiplicative hash: neighbours spread
		h ^= h >> 16;
		return lo + (hi - lo) * (float)(h & 0xFFFF) / 65535.0f;
	}

	bool GripCentre(const V3 joints[4][3], float maxRadius, V3& centre)
	{
		V3 sum{};
		int curled = 0;
		for (int f = 0; f < 4; f++) {
			// the circle through three points: C + ((|a|^2 b - |b|^2 a) x (a x b)) / (2 |a x b|^2)
			const V3& c = joints[f][2];
			V3 a = Sub(joints[f][0], c), b = Sub(joints[f][1], c);
			V3 n = Cross(a, b);
			float nn = Dot(n, n);
			if (nn < 1e-8f)
				continue;                             // in line: a straight finger holds nothing
			V3 at = Add(c, Scale(Cross(Sub(Scale(b, Dot(a, a)), Scale(a, Dot(b, b))), n), 0.5f / nn));
			if (Length(Sub(at, c)) > maxRadius)
				continue;                             // barely bent: its circle's centre is nowhere near a shaft
			sum = Add(sum, at);
			curled++;
		}
		if (curled < 2)
			return false;
		centre = Scale(sum, 1.0f / curled);
		return true;
	}

	bool Held(const Chain& c, const std::vector<V3>& joints, const std::vector<Hand>& hands, const Params& p)
	{
		// the shaft's outer part, from the first joint past the root to the tip: a hand at its base (a
		// partner steadying it, his own hand at his groin) does not count
		for (auto& h : hands)
			for (size_t i = 1; i + 1 < joints.size(); i++)
				if (SegmentDistance(h.point, joints[i], joints[i + 1]) <= p.handRadius)
					return true;
		(void)c;
		return false;
	}

	std::vector<Quat> Bend(const Chain& c, const Target& t, float stretch)
	{
		size_t n = c.locals.size();
		std::vector<Quat> out(n ? n - 1 : 0);
		if (n < 2)
			return out;
		// the line to lay the chain on: root, entrance, then the path inside (or straight in)
		std::vector<V3> line{ c.root, t.point };
		if (!t.path.empty())
			line.insert(line.end(), t.path.begin(), t.path.end());
		else
			line.push_back(Add(t.point, Scale(t.in, 24.0f)));
		// Each joint, root first, turned so the next lands ON the line, exactly a bone's length on from it:
		// the next crossing of the line with a sphere of that radius. (A point that far ALONG the line
		// would put the joint off it wherever a bone spans a bend: the chord is shorter than the arc.)
		size_t seg = 1;
		float from = 0.0f;                           // the current joint's place on line[seg - 1] -> line[seg]
		Quat parentW = c.parent;
		V3 pos = c.root;
		for (size_t i = 0; i + 1 < n; i++) {
			Quat w = Mul(parentW, c.locals[i]);
			V3 step = Rotate(w, Scale(c.offsets[i + 1], stretch));
			float len = Length(step);
			V3 next;
			bool found = false;
			while (seg < line.size() && !found) {
				V3 a = line[seg - 1], d = Sub(line[seg], a), m = Sub(a, pos);
				float A = Dot(d, d), B = 2.0f * Dot(m, d), C = Dot(m, m) - len * len;
				float disc = B * B - 4.0f * A * C;
				if (A > 1e-8f && disc >= 0.0f) {
					float t = (-B + std::sqrt(disc)) / (2.0f * A);   // the forward crossing
					if (t >= from && t <= 1.0f) {
						next = Add(a, Scale(d, t));
						from = t;
						found = true;
						break;
					}
				}
				seg++;
				from = 0.0f;
			}
			if (!found) {                            // past the line's end: straight on along its last leg
				V3 last = Normalized(Sub(line.back(), line[line.size() - 2]));
				next = Add(pos, Scale(last, len));
				seg = line.size();
			}
			Quat turn = FromTo(Normalized(step), Normalized(Sub(next, pos)));
			out[i] = Mul(Mul(Conj(parentW), turn), parentW);      // in the parent's frame
			Quat turned = Mul(turn, w);
			pos = Add(pos, Rotate(turned, Scale(c.offsets[i + 1], stretch)));
			parentW = turned;
		}
		return out;
	}

	Result Update(State& s, const Chain& c, const std::vector<Target>& targets, const std::vector<Hand>& hands,
		const Params& p, float dt)
	{
		Result r;
		size_t n = c.locals.size();
		size_t joints = n ? n - 1 : 0;
		if (s.correction.size() != joints)
			s.correction.assign(joints, Quat{});
		s.clock += (std::max)(0.0f, dt);

		std::vector<V3> pose;
		std::vector<Quat> world;
		Pose(c, c.locals, 1.0f, pose, world);

		// a hand on the shaft: it is being held, not put in (and a moment after, so a stroke that leaves
		// the shaft for a frame does not snap it into her)
		r.held = Held(c, pose, hands, p);
		if (r.held)
			s.heldUntil = s.clock + p.handHold;
		bool blocked = s.clock < s.heldUntil;

		// what may be entered now: while a hand grips, only a hand (the shaft through its grip); just after,
		// nothing; otherwise anything but a hand
		auto allowed = [&](const Target& t) { return r.held ? t.kind == kHand : !blocked && t.kind != kHand; };
		Fit best;
		Target chosenT;
		const Target* chosen = nullptr;
		bool wasLocked = s.locked;
		std::uint32_t wasOwner = s.lockedOwner;
		int wasKind = s.lockedKind;
		const char* lost = "it is gone";
		if (s.locked) {                             // a lock held stays while it still fits, loosely
			for (auto& t : targets) {
				if (t.owner == s.lockedOwner && t.kind == s.lockedKind) {
					Target o = Oriented(t, pose);
					Fit f;
					if (allowed(t))
						f = Judge(c, pose, o, p, true);
					else
						f.why = r.held ? "a hand holds the shaft" : "a hand just let go";
					if (f.ok) {
						best = f;
						chosenT = o;
						chosen = &chosenT;
					}
					else
						lost = f.why;
					break;
				}
			}
		}
		if (!chosen) {                              // otherwise the one the animation came closest to entering
			for (auto& t : targets) {
				if (!allowed(t))
					continue;
				Target o = Oriented(t, pose);
				Fit f = Judge(c, pose, o, p, false);
				if (f.ok && (!chosen || f.miss < best.miss)) {
					best = f;
					chosenT = o;
					chosen = &chosenT;
				}
			}
			r.newLock = chosen != nullptr;
		}
		s.locked = chosen != nullptr;
		s.lockedOwner = chosen ? chosen->owner : 0;
		s.lockedKind = chosen ? chosen->kind : -1;

		if (wasLocked && !chosen) {
			r.released = true;
			r.why = lost;
		}
		if (s.want.size() != joints)
			s.want.assign(joints, Quat{});
		float a = dt > 0.0f ? 1.0f - std::exp(-p.rate * dt) : 0.0f;
		// another opening than the one showing (a switch, or a new lock while the last still fades): the
		// pose on screen fades into the new one instead of jumping
		bool other = chosen && (!wasLocked || wasOwner != chosen->owner || wasKind != chosen->kind);
		if (other && s.weight > 1e-3f) {
			s.from = s.correction;
			s.fromStretch = s.stretch;
			s.fromFade = 1.0f;
		}
		if (chosen) {                               // exact, every frame: no lag behind a moving head
			s.want = Bend(c, *chosen, best.stretch);
			s.wantStretch = best.stretch;
		}
		s.weight += ((chosen ? 1.0f : 0.0f) - s.weight) * a;
		if (s.weight > 0.999f)
			s.weight = 1.0f;
		if (s.weight < 1e-4f)
			s.weight = 0.0f;
		bool moving = false;
		for (size_t i = 0; i < joints; i++) {
			Quat q = Slerp(Quat{}, s.want[i], s.weight);
			if (s.fromFade > 0.0f && i < s.from.size())
				q = Slerp(q, s.from[i], s.fromFade);
			s.correction[i] = q;
			moving = moving || Angle(q) >= 1e-4f;
		}
		s.stretch = 1.0f + (s.wantStretch - 1.0f) * s.weight;
		if (s.fromFade > 0.0f)
			s.stretch += (s.fromStretch - s.stretch) * s.fromFade;
		s.fromFade *= (1.0f - a);
		if (s.fromFade < 1e-3f)
			s.fromFade = 0.0f;

		r.local = s.correction;
		r.stretch = s.stretch;
		r.locked = s.locked;
		r.targetOwner = s.lockedOwner;
		r.targetKind = s.lockedKind;
		r.angle = chosen ? best.angle : 0.0f;
		r.miss = chosen ? best.miss : 0.0f;
		r.active = chosen || moving || std::fabs(s.stretch - 1.0f) >= 1e-4f;
		return r;
	}
}
