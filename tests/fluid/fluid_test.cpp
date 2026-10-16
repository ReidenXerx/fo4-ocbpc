// fo4-ocbpc: the climax fluid's particles (FluidSim.h), tested outside the game. Build and run: tests/fluid/run.bat.
// Metres, z up, the floor at z = 0, a step of 1/120 s as Fluid.cpp takes them. Each check states what it holds.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "FluidSim.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace FluidSim;

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

constexpr float h = 0.006f;
constexpr float dt = 1.0f / 120.0f;

static float Len(const V& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

static Particle At(const V& p, const V& v = V(0, 0, 0))
{
	Particle q;
	q.pos = p;
	q.vel = v;
	q.life = 60.0f;
	return q;
}

static bool AllFinite(const std::vector<Particle>& ps)
{
	for (const auto& p : ps)
		if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || !std::isfinite(p.pos.z))
			return false;
	return true;
}

int main()
{
	const Params P;
	// 1. shots landing on a body-sized ball (r 10 cm like the engine's breast and belly spheres, centre 40 cm up): three
	//    runs of 10 dropped from 2 cm above it at a_angle off its top, spread around it. After 5 s: how many are still
	//    on it, how far down they ran (the mean angle off the top, degrees), any inside it (never)
	struct Landed
	{
		int on = 0, inside = 0;
		float angle = 0.0f;
		bool finite = true;
	};
	constexpr float R = 0.10f;
	const V C(0, 0, 0.40f);
	auto land = [&](const Params& a_params, float a_angle) {
		Solver s(h);
		std::vector<Particle> ps;
		std::vector<Anchor> anchors;
		const std::vector<Capsule> ball{ Capsule{ C, C, R } };
		const float t = a_angle * 3.14159265f / 180.0f;
		for (int run = 0; run < 3; run++) {
			const float az = run * 2.0944f;
			const V n(std::sin(t) * std::cos(az), std::sin(t) * std::sin(az), std::cos(t));
			const V side(-std::sin(az), std::cos(az), 0.0f);
			for (int k = 0; k < 10; k++) {
				ps.push_back(At(C + n * (R + 0.02f) + side * (k * 0.9f * h), V(0, 0, -1.0f)));
				if (k < 9)
					ps.back().link = 0.9f * h;
			}
		}
		for (int step = 0; step < (int)(5.0f / dt); step++) {
			s.Step(ps, anchors, ball, {}, a_params, dt);
			if (std::getenv("FLUID_TRACE") && a_params.grip == 2.5f && a_params.friction == 120.0f && a_angle == 45.0f && step % 15 == 0 && step < 600) {
				const Particle& p = ps[5];
				const V off = p.pos - C;
				const float d = Len(off);
				std::printf("      trace %3d: angle %5.1f  off-skin %+.4f  touching %d  surface %d  v (%+.3f %+.3f %+.3f)  link %.4f\n", step,
					std::acos(off.z / d) * 57.2958f, d - R, p.touching, p.surface, p.vel.x, p.vel.y, p.vel.z, p.link);
			}
		}
		Landed r;
		for (const auto& p : ps) {
			const V off = p.pos - C;
			const float d = Len(off);
			r.inside += d < R - 1e-4f;
			if (d < R + 3.0f * h) {
				r.on++;
				r.angle += std::acos(std::fmax(-1.0f, std::fmin(1.0f, off.z / d))) * 180.0f / 3.14159265f;
			}
		}
		r.angle = r.on ? r.angle / r.on : 0.0f;
		r.finite = AllFinite(ps);
		return r;
	};
	if (std::getenv("FLUID_SWEEP"))
		for (float friction : { 30.0f, 60.0f, 120.0f })
			for (float grip : { 1.5f, 2.0f, 2.5f })
				for (float angle : { 10.0f, 45.0f, 75.0f }) {
					Params q = P;
					q.grip = grip;
					q.friction = friction;
					const Landed r = land(q, angle);
					std::printf("      friction %3.0f grip %.1f, landing %2.0f deg off the top: after 5 s %2d of 30 on it, at %5.1f deg\n",
						friction, grip, angle, r.on, r.angle);
				}
	{
		const Landed top = land(P, 10.0f), slope = land(P, 45.0f), side = land(P, 75.0f);
		std::printf("      defaults: top %d at %.1f, slope %d at %.1f, side %d at %.1f\n", top.on, top.angle, slope.on, slope.angle, side.on,
			side.angle);
		Expect(top.finite && slope.finite && side.finite, "every particle stays finite");
		Expect(top.inside == 0 && slope.inside == 0 && side.inside == 0, "none ends inside the ball");
		Expect(top.on >= 24 && top.angle < 40.0f, "shots landing on top of a body stay near where they landed");
		Expect(slope.on >= 24 && slope.angle > 65.0f, "shots landing on a slope stay on the body but run down it");
		Expect(side.on < 30, "shots landing on a steep side run off and drip");
	}
	// 2. a puddle on the floor settles and sleeps
	{
		Solver s(h);
		std::vector<Particle> ps;
		std::vector<Anchor> anchors;
		for (int x = 0; x < 6; x++)
			for (int y = 0; y < 6; y++)
				ps.push_back(At(V(x * h, y * h, 0.5f * h + 0.004f)));
		for (int step = 0; step < 360; step++)
			s.Step(ps, anchors, {}, {}, P, dt);
		int asleep = 0;
		for (const auto& p : ps)
			asleep += p.asleep;
		std::printf("      asleep %d of %zu\n", asleep, ps.size());
		Expect(asleep == (int)ps.size(), "a puddle on the floor falls asleep within 3 s");
	}
	// 3. a drop resting on a capsule that moves sideways at 0.3 m/s is carried with it
	{
		Solver s(h);
		std::vector<Particle> ps;
		std::vector<Anchor> anchors;
		for (int i = 0; i < 6; i++)
			ps.push_back(At(V(0.002f * (i % 3), 0.002f * (i / 3), 0.20f + 0.05f + 0.5f * h + 0.0005f)));
		float x = 0.0f;
		for (int step = 0; step < 120; step++) {
			const float v = 0.3f;
			x += v * dt;
			const std::vector<Capsule> ball{ Capsule{ V(x, 0, 0.20f), V(x, 0, 0.20f), 0.05f, V(v, 0, 0), V(v, 0, 0) } };
			s.Step(ps, anchors, ball, {}, P, dt);
		}
		int carried = 0;
		for (const auto& p : ps)
			carried += Len(p.pos - V(x, 0, 0.20f)) < 0.05f + 3.0f * h;
		std::printf("      ball moved %.3f m: %d of %zu still on it\n", x, carried, ps.size());
		Expect(carried >= 4, "a drop on a moving body rides it");
	}
	// 4. a link stretched far past its rest snaps
	{
		Solver s(h);
		std::vector<Particle> ps{ At(V(0, 0, 0.5f), V(-3, 0, 0)), At(V(h, 0, 0.5f), V(3, 0, 0)) };
		ps[0].link = h;
		std::vector<Anchor> anchors;
		for (int step = 0; step < 30; step++)
			s.Step(ps, anchors, {}, {}, P, dt);
		Expect(ps[0].link == 0.0f, "two linked particles flung apart lose their link");
	}
	// 5. a packed clump reads as fluid and wraps: the centre is inside, the skin is out past the clump's half-width
	{
		std::vector<Particle> ps;
		for (int x = -2; x <= 2; x++)
			for (int y = -2; y <= 2; y++)
				for (int z = -2; z <= 2; z++)
					ps.push_back(At(V(x * h, y * h, 0.1f + z * h)));
		const Density field(ps, 2.0f * h, 6.0f);
		std::vector<V> dirs{ V(1, 0, 0), V(-1, 0, 0), V(0, 1, 0), V(0, -1, 0), V(0, 0, 1), V(0, 0, -1) };
		std::vector<SurfacePoint> out;
		const bool ok = Wrap(field, V(0, 0, 0.1f), 6.0f * h, 1.75f, dirs, out);
		Expect(ok, "the clump's centre is inside the field");
		float lo = 1e9f, hi = 0.0f;
		for (const auto& sp : out)
			lo = (std::min)(lo, sp.dist), hi = (std::max)(hi, sp.dist);
		std::printf("      skin at %.4f .. %.4f m (half-width %.4f)\n", lo, hi, 2.0f * h);
		Expect(ok && lo > 2.0f * h && hi < 4.5f * h, "the skin sits just outside the clump on every side");
		Expect(ok && Len(out[4].normal - V(0, 0, 1)) < 0.2f, "the top's normal points up");
		const auto clusters = FindClusters(ps, [&] {
			std::vector<std::size_t> all(ps.size());
			for (std::size_t i = 0; i < all.size(); i++)
				all[i] = i;
			return all;
		}(), 2.0f * h);
		Expect(clusters.size() == 1 && clusters[0].members.size() == ps.size(), "one clump is one cluster");
	}
	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
