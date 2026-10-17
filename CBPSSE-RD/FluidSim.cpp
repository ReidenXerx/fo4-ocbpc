// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16 (see FluidSim.h; the method and constants follow
// OStrands' fluid, GPL-3, by LilRicefield).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "FluidSim.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace FluidSim
{
	namespace
	{
		// OStrands' solver constants (metres, seconds)
		constexpr float kMaxSpeed = 6.0f;
		constexpr float kFloorFriction = 8.0f;
		constexpr float kLinkStiffness = 0.6f;
		constexpr float kLinkBreak = 12.0f;     // a link's rest past this many spacings: it lets go
		constexpr float kLinkSnap = 6.0f;       // stretched past this many times its rest: it snaps
		constexpr float kSleepSpeed = 0.02f, kWakeSpeed = 0.08f, kSleepTime = 0.4f;
		constexpr float kSkinGravity = 0.25f;   // what of gravity pulls a particle that touches a body
		constexpr float kStickReach = 2.0f;     // spacings off a body a touching particle is still pulled onto it
		constexpr float kHanging = -0.5f;       // under a body (normal pointing down more than this): no pull, it drips
		constexpr float kSeparation = 0.1f;     // m/s off a body at most, per step
		constexpr float kAnchorBreak = 12.0f, kAnchorFlow = 15.0f, kCreep = 0.035f;
		constexpr float kRoomSpacing = 1.5f;   // particles in a canal sit this many spacings apart (tests/fluid: a closed
		                                       // canal keeps a quarter of what it held and gushes the rest)
		constexpr float kSqueeze = 0.4f, kSqueezeMax = 0.3f;   // m/s out of a canal per share it is over its room, at most
		constexpr float kStringBreak = 30.0f;   // a slime string lets go past this many spacings (18 cm)
		constexpr float kStringAfter = 0.1f;    // seconds on a body before leaving it pulls a string (not a flicker)
		constexpr float kStringLanded = 0.5f;   // seconds back on a body before its string lets go (a hanging drop brushes
		                                        // the skin it hangs from: dropping the string at once cut every one at 0.7 cm)
		constexpr std::size_t kMaxLoad = 64;
		constexpr float kCohesionFloor = 1.0f;
		// two linked particles both on a body this long: the thread has landed and is a smear now; its links let go,
		// or the part hanging past a body's edge dragged all of it off through them, whatever the grip (tests/fluid)
		constexpr float kLanded = 0.3f;

		float Dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		float Len2(const V& a) { return Dot(a, a); }
		float Len(const V& a) { return std::sqrt(Dot(a, a)); }
		V Limit(const V& a, float m)
		{
			const float l = Len(a);
			return l > m ? a * (m / l) : a;
		}
		V Unit(const V& a, const V& fb)
		{
			const float l = Len(a);
			return l > 1e-9f ? a * (1.0f / l) : fb;
		}
		bool Finite(const V& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
	}

	void Grid::Build(const std::vector<V>& a_points, float a_cell)
	{
		cell = a_cell;
		for (auto& [k, v] : cells)
			v.clear();
		for (std::size_t i = 0; i < a_points.size(); i++)
			cells[Key(Cell(a_points[i].x), Cell(a_points[i].y), Cell(a_points[i].z))].push_back(i);
		if (cells.size() > 4 * a_points.size() + 64)
			std::erase_if(cells, [](const auto& e) { return e.second.empty(); });
	}

	Solver::Solver(float a_spacing) :
		h(a_spacing), R2(4.0f * a_spacing * a_spacing)
	{
		// the kernel sum a particle sees at rest in a lattice of spacing h: the density the constraint keeps
		for (int z = -2; z <= 2; z++)
			for (int y = -2; y <= 2; y++)
				for (int x = -2; x <= 2; x++)
					rest += Kernel(V((float)x, (float)y, (float)z) * h);
	}

	float Solver::Kernel(const V& d) const
	{
		const float q = (std::max)(0.0f, 1.0f - Len2(d) / R2);
		return q * q * q;
	}

	V Solver::KernelGradient(const V& d) const
	{
		const float q = (std::max)(0.0f, 1.0f - Len2(d) / R2);
		return d * (-6.0f * q * q / (R2 * rest));
	}

	void Solver::Neighbours(const std::vector<Particle>& a_particles, bool a_all)
	{
		grid.Build(predicted, 2.0f * h);
		near.resize(predicted.size());
		for (std::size_t i = 0; i < predicted.size(); i++) {
			auto& n = near[i];
			n.clear();
			if (!a_all && a_particles[i].asleep)
				continue;
			grid.Near(predicted[i], [&](std::size_t j) {
				if (j != i && Len2(predicted[i] - predicted[j]) < R2)
					n.push_back(j);
			});
		}
	}

	float Solver::SkinDrag(const Params& a_params, float a_crowd) const
	{
		// how thin the fluid is here: a lone particle 1, 0.8 of a lattice's neighbours or more 0 (tests/fluid: a glob runs at
		// 2 cm/s and leaves 6 of 20 behind as its trail; 0.7 left 2, 0.6 with skinCohesion 0.15 left half)
		const float thin = std::clamp(1.0f - a_crowd / (0.8f * rest), 0.0f, 1.0f);
		return a_params.friction * (1.0f + a_params.film * thin * thin);
	}

	V Solver::CanalCarrier(const Canal& a_canal, const V& a_point)
	{
		const V span = a_canal.b - a_canal.a;
		const float l2 = Len2(span);
		const float t = l2 > 1e-12f ? std::clamp(Dot(a_point - a_canal.a, span) / l2, 0.0f, 1.0f) : 0.0f;
		return a_canal.va + (a_canal.vb - a_canal.va) * t;
	}

	void Solver::Contain(std::vector<Particle>& a_particles, const std::vector<Canal>& a_canals, bool a_join)
	{
		for (std::size_t i = 0; i < predicted.size(); i++) {
			Particle& q = a_particles[i];
			if (q.canal >= (int)a_canals.size())
				q.canal = -1;
			V& p = predicted[i];
			if (q.canal < 0) {
				if (!a_join)
					continue;
				for (std::size_t k = 0; k < a_canals.size() && q.canal < 0; k++) {   // in through the entrance
					const Canal& c = a_canals[k];
					const V span = c.b - c.a;
					const float l2 = Len2(span);
					if (l2 < 1e-12f)
						continue;
					const float t = Dot(p - c.a, span) / l2;
					if (t >= 0.0f && t <= 1.5f && Len2(p - (c.a + span * std::min(t, 1.0f))) < c.r * c.r)   // past its end
						// too: his tip can sit a little deeper than the canal reaches, and what leaves it belongs in her
						q.canal = (int)k, q.asleep = false, q.touching = false, q.surface = -1;
				}
				if (q.canal < 0)
					continue;
			}
			const Canal& c = a_canals[q.canal];
			const V span = c.b - c.a;
			const float len = Len(span);
			if (len < 1e-6f) {
				q.canal = -1;
				continue;
			}
			const V axis = span * (1.0f / len);
			float s = Dot(p - c.a, axis);
			if (s < -0.5f * h) {   // out past the entrance: it is outside her now
				q.canal = -1;
				continue;
			}
			s = (std::min)(s, len - 0.5f * h);   // the deep end is closed
			const V on = c.a + axis * s;
			V off = p - on;
			const float d = Len(off);
			const float inner = (std::max)(c.r - 0.5f * h, 0.3f * h);
			if (d > inner)
				off = off * (inner / d);
			p = on + off;
		}
	}

	V Solver::Carrier(const std::vector<Capsule>& a_capsules, int a_surface, float a_along)
	{
		if (a_surface < 0 || (std::size_t)a_surface >= a_capsules.size())
			return V(0, 0, 0);
		const Capsule& c = a_capsules[a_surface];
		return c.va + (c.vb - c.va) * a_along;
	}

	void Solver::Collide(const std::vector<Particle>& a_particles, const std::vector<Capsule>& a_capsules,
		const std::vector<Hole>& a_holes, bool a_pull)
	{
		const float margin = 0.5f * h;
		const float reach = margin + kStickReach * h;
		for (std::size_t i = 0; i < predicted.size(); i++) {
			if (a_particles[i].asleep)
				continue;
			V& p = predicted[i];
			const bool inCanal = a_particles[i].canal >= 0;
			bool inHole = false;
			for (const Hole& hole : a_holes)
				inHole = inHole || Len2(p - hole.c) < hole.r * hole.r;
			if (inHole)
				continue;
			for (std::size_t k = 0; k < a_capsules.size(); k++) {
				const Capsule& c = a_capsules[k];
				if (inCanal && !c.shaft)   // inside her: her body's capsules (a crude hull around the canal) do not push
					continue;
				const float ext = c.r + reach;
				if (p.x < (std::min)(c.a.x, c.b.x) - ext || p.x > (std::max)(c.a.x, c.b.x) + ext ||
					p.y < (std::min)(c.a.y, c.b.y) - ext || p.y > (std::max)(c.a.y, c.b.y) + ext ||
					p.z < (std::min)(c.a.z, c.b.z) - ext || p.z > (std::max)(c.a.z, c.b.z) + ext)
					continue;
				const V span = c.b - c.a;
				const float l2 = Len2(span);
				const float t = l2 > 1e-12f ? std::clamp(Dot(p - c.a, span) / l2, 0.0f, 1.0f) : 0.0f;
				const V closest = c.a + span * t;
				const V off = p - closest;
				const float dist = Len(off);
				const float limit = c.r + margin;
				const V n = dist > 1e-7f ? off * (1.0f / dist) : V(0, 0, 1);
				if (dist < limit)
					p = closest + n * limit;
				else if (a_pull && a_particles[i].touching && dist < c.r + reach && n.z > kHanging)
					p = closest + n * limit;   // still wet on it: drawn back on (not under it, where it drips)
				else if (!a_pull && dist < limit + 0.5f * h) {
					// resting on it, not in it: still a contact. Counting only a particle pushed out let one that sat
					// exactly on the surface lose its contact every other step, and with it the grip: every drop slid
					// off a body-sized sphere in 5 s at any stickiness (tests/fluid, 10-17)
				} else
					continue;
				contact[i] = (int)k;
				along[i] = t;
				normal[i] = n;
			}
		}
	}

	void Solver::Step(std::vector<Particle>& a_particles, std::vector<Anchor>& a_anchors, const std::vector<Capsule>& a_capsules,
		const std::vector<Hole>& a_holes, const Params& a_params, float a_dt, const std::vector<Canal>& a_canals)
	{
		const std::size_t n = a_particles.size();
		if (n == 0 || !(a_dt > 0.0f))
			return;
		for (Particle& p : a_particles)   // a broken value never spreads: that particle goes
			if (!Finite(p.pos) || !Finite(p.vel))
				p = Particle{}, p.life = 0.0f;
		predicted.resize(n);
		velocity.resize(n);
		correction.resize(n);
		lambda.resize(n);
		normal.resize(n);
		along.resize(n);
		contact.assign(n, -1);
		anchored.assign(n, 0);
		for (Anchor& a : a_anchors) {
			if (a.surface >= 0) {   // a slime string's root rides its body
				if ((std::size_t)a.surface < a_capsules.size())
					a.point += Carrier(a_capsules, a.surface, a.along) * a_dt;
				else
					a.rest = 0.0f;
			}
			if (a.rest > 0.0f && a.particle < n)
				anchored[a.particle] = 1;
		}
		lastSurface.resize(n);
		lastAlong.resize(n);
		lastStuck.resize(n);
		for (std::size_t i = 0; i < n; i++)
			lastSurface[i] = a_particles[i].touching ? a_particles[i].surface : -1, lastAlong[i] = a_particles[i].along,
			lastStuck[i] = a_particles[i].stuck;
		const float lift = 0.5f * h;   // the floor: z = lift (a particle's centre half a spacing above the plane)
		for (std::size_t i = 0; i < n; i++)
			predicted[i] = a_particles[i].pos;
		Neighbours(a_particles, true);
		for (Particle& p : a_particles)
			if (p.canal >= (int)a_canals.size())
				p.canal = -1;
		// a canal holding more than it has room for pushes the extra out of its entrance: she closes after he pulls out
		// (her canal narrows) and squeezes it out, a gush. Its room: its inside's volume over a particle's, loosely packed
		// Only the extra goes, the ones nearest the entrance: pushing every one piled what stays at the entrance, and it all
		// ran out in 6 s (tests/fluid)
		squeeze.assign(a_canals.size(), 0.0f);
		pushed.assign(n, 0);
		if (!a_canals.empty()) {
			std::vector<std::vector<std::pair<float, std::size_t>>> members(a_canals.size());
			for (std::size_t i = 0; i < n; i++)
				if (a_particles[i].canal >= 0)
					members[a_particles[i].canal].emplace_back(Dot(a_particles[i].pos - a_canals[a_particles[i].canal].a,
						Unit(a_canals[a_particles[i].canal].b - a_canals[a_particles[i].canal].a, V(0, 0, 1))), i);
			for (std::size_t k = 0; k < a_canals.size(); k++) {
				const Canal& c = a_canals[k];
				const float inner = (std::max)(c.r - 0.5f * h, 0.3f * h);
				const float room = (std::max)(1.0f, 3.14159265f * inner * inner * Len(c.b - c.a) / std::pow(kRoomSpacing * h, 3.0f));
				auto& m = members[k];
				const float over = (float)m.size() / room - 1.0f;
				if (over <= 0.0f)
					continue;
				squeeze[k] = (std::min)(kSqueeze * over, kSqueezeMax);
				const std::size_t extra = m.size() - (std::size_t)room;
				std::partial_sort(m.begin(), m.begin() + extra, m.end());
				for (std::size_t j = 0; j < extra; j++)
					pushed[m[j].second] = 1;
			}
		}

		// a sleeper wakes when it is held or a neighbour moves fast
		for (std::size_t i = 0; i < n; i++) {
			Particle& p = a_particles[i];
			if (!p.asleep)
				continue;
			bool stirred = anchored[i] != 0;
			for (std::size_t j : near[i])
				stirred = stirred || (!a_particles[j].asleep && Len2(a_particles[j].vel) > kWakeSpeed * kWakeSpeed);
			if (stirred)
				p.asleep = false, p.idle = 0.0f;
		}

		// predict: cohesion toward the neighbours (shared by how crowded both are), riding the surface it touches,
		// gravity (a quarter of it on a body)
		weights.assign(n, 0.0f);
		for (std::size_t i = 0; i < n; i++)
			for (std::size_t j : near[i])
				weights[i] += Kernel(a_particles[j].pos - a_particles[i].pos);
		for (std::size_t i = 0; i < n; i++) {
			const Particle& p = a_particles[i];
			if (p.asleep)
				continue;
			V pull(0, 0, 0);
			for (std::size_t j : near[i]) {
				const V d = a_particles[j].pos - p.pos;
				const float shared = (std::max)(0.5f * (weights[i] + weights[j]), kCohesionFloor);
				pull += d * (Kernel(d) / shared);
			}
			pull = pull * (a_params.cohesion * (p.touching || p.canal >= 0 ? a_params.skinCohesion : 1.0f) / h);   // in a canal too:
			// what already ran out pulled the rest after it (tests/fluid: a closed canal emptied in 12 s at any drag)
			V moving = p.vel;
			const float g = p.touching ? a_params.gravity * kSkinGravity : a_params.gravity;
			V accel = pull + V(0, 0, -g);
			if (p.canal >= 0) {   // inside her: moves with the walls, flows along them slowly
				const V carrier = CanalCarrier(a_canals[p.canal], p.pos);
				moving = carrier + (moving - carrier + accel * a_dt) * (1.0f / (1.0f + a_params.canalDrag * a_dt));
				const Canal& c = a_canals[p.canal];
				if (pushed[i])
					moving = moving - Unit(c.b - c.a, V(0, 0, 1)) * squeeze[p.canal];
				accel = V(0, 0, 0);
			} else if (p.touching && p.surface >= 0 && (std::size_t)p.surface < a_capsules.size()) {
				const V carrier = Carrier(a_capsules, p.surface, p.along);
				moving = moving - p.carried + carrier;
				// sliding along the skin: drag taken implicitly here (the speed it settles at is the push over the drag,
				// whatever the step; drag applied only after the move let gravity creep everything a step's worth), then
				// the grip: a push under it does not move the particle at all (10-17: the grip zeroed only the velocity,
				// after gravity had already moved the position, and every held drop crept)
				const Capsule& c = a_capsules[p.surface];
				const V n = Unit(p.pos - (c.a + (c.b - c.a) * p.along), V(0, 0, 1));
				const V rel = moving - carrier;
				const V relT = rel - n * Dot(rel, n), accT = accel - n * Dot(accel, n);
				V slide = (relT + accT * a_dt) * (1.0f / (1.0f + SkinDrag(a_params, weights[i]) * a_dt));
				if (Len(slide) <= a_params.grip * a_dt)
					slide = V(0, 0, 0);
				moving = carrier + n * Dot(rel, n) + slide;
				accel = n * Dot(accel, n);
			}
			const V v = moving + accel * a_dt;
			predicted[i] = p.pos + Limit(v, kMaxSpeed) * a_dt;
			predicted[i].z = (std::max)(predicted[i].z, lift);
		}
		Collide(a_particles, a_capsules, a_holes, true);
		Contain(a_particles, a_canals, true);

		for (int it = 0; it < a_params.iterations; it++) {
			Neighbours(a_particles, false);
			// the density constraint, one-sided: only a crowd pushes apart (cohesion holds them together)
			for (std::size_t i = 0; i < n; i++) {
				if (a_particles[i].asleep) {
					lambda[i] = 0.0f;
					continue;
				}
				float density = 1.0f;
				float denom = 0.01f / (h * h);
				V sum(0, 0, 0);
				for (std::size_t j : near[i]) {
					const V d = predicted[i] - predicted[j];
					density += Kernel(d);
					const V g = KernelGradient(d);
					sum += g;
					denom += Dot(g, g);
				}
				denom += Dot(sum, sum);
				lambda[i] = -(std::max)(0.0f, density / rest - 1.0f) / denom;
			}
			for (std::size_t i = 0; i < n; i++) {
				V c(0, 0, 0);
				if (!a_particles[i].asleep)
					for (std::size_t j : near[i])
						c += KernelGradient(predicted[i] - predicted[j]) * (lambda[i] + lambda[j]);
				correction[i] = Limit(c, 0.25f * h);
			}
			for (std::size_t i = 0; i < n; i++)
				predicted[i] += correction[i];
			// a run's links: the next one held at the rest length (a thread pulls and pushes a little)
			for (std::size_t i = 0; i + 1 < n; i++) {
				const float restLen = a_particles[i].link;
				if (restLen <= 0.0f)
					continue;
				const bool first = !a_particles[i].asleep, second = !a_particles[i + 1].asleep;
				const V d = predicted[i + 1] - predicted[i];
				const float l = Len(d);
				if (l < 1e-6f || !(first || second))
					continue;
				const float share = first && second ? 0.5f : 1.0f;
				const V fix = Limit(d * ((l - restLen) / l * share * kLinkStiffness), 0.25f * h);
				if (first)
					predicted[i] += fix;
				if (second)
					predicted[i + 1] -= fix;
			}
			for (const Anchor& a : a_anchors) {   // a rope from its source: pulled back when past its rest
				if (a.rest <= 0.0f || a.particle >= n)
					continue;
				V& p = predicted[a.particle];
				const V d = p - a.point;
				const float l = Len(d);
				if (l > a.rest)
					p -= Limit(d * ((l - a.rest) / l * kLinkStiffness), 0.25f * h);
			}
			for (std::size_t i = 0; i < n; i++)
				predicted[i].z = (std::max)(predicted[i].z, lift);
			Contain(a_particles, a_canals, false);
		}

		// viscosity of a thread: a stretched link's rest flows after its length; too long, it lets go or snaps
		const float flow = (std::min)(1.0f, a_params.stretch * a_dt);
		for (std::size_t i = 0; i + 1 < n; i++) {
			float& r = a_particles[i].link;
			if (r <= 0.0f)
				continue;
			const float l = Len(predicted[i + 1] - predicted[i]);
			if (l > r)
				r += (l - r) * flow;
			if (r > kLinkBreak * h || l > kLinkSnap * r || (a_particles[i].stuck > kLanded && a_particles[i + 1].stuck > kLanded))
				r = 0.0f;
		}
		Collide(a_particles, a_capsules, a_holes, false);
		Contain(a_particles, a_canals, false);
		for (std::size_t i = 0; i < n; i++)
			predicted[i].z = (std::max)(predicted[i].z, lift);

		// anchors give way as the rope hangs: the heavier the run below, the faster
		const float follow = (std::min)(1.0f, kAnchorFlow * a_dt), stringFollow = (std::min)(1.0f, a_params.slime * a_dt);
		for (Anchor& a : a_anchors) {
			if (a.rest <= 0.0f || a.particle >= n) {
				a.rest = 0.0f;
				continue;
			}
			const V d = predicted[a.particle] - a.point;
			const float l = Len(d);
			if (l > a.rest)
				a.rest += (l - a.rest) * (a.surface >= 0 ? stringFollow : follow);
			if (d.z < 0.0f) {
				std::size_t load = 1;
				for (std::size_t k = a.particle; k > 0 && load < kMaxLoad && a_particles[k - 1].link > 0.0f; k--)
					load++;
				a.rest += kCreep * std::sqrt((float)load) * a_dt;
			}
			if (a.rest > (a.surface >= 0 ? kStringBreak : kAnchorBreak) * h)
				a.rest = 0.0f;
		}

		// velocities from the motion, smoothed among neighbours (XSPH viscosity); on a body: carried by it, held by grip,
		// slowed by friction, never pressed into it nor flung off faster than kSeparation; on the floor: friction
		for (std::size_t i = 0; i < n; i++)
			velocity[i] = a_particles[i].asleep ? V(0, 0, 0) : Limit((predicted[i] - a_particles[i].pos) * (1.0f / a_dt), kMaxSpeed);
		Neighbours(a_particles, false);
		for (std::size_t i = 0; i < n; i++) {
			weights[i] = 0.0f;
			for (std::size_t j : near[i])
				weights[i] += Kernel(predicted[i] - predicted[j]);
		}
		const float visc = 1.0f - std::exp(-a_params.viscosity * a_dt);
		const float floorKeep = std::exp(-kFloorFriction * a_dt);
		for (std::size_t i = 0; i < n; i++) {
			Particle& p = a_particles[i];
			if (p.asleep) {
				p.vel = V(0, 0, 0);
				continue;
			}
			V smooth(0, 0, 0);
			for (std::size_t j : near[i]) {
				const float shared = 1.0f + 0.5f * (weights[i] + (a_particles[j].asleep ? weights[i] : weights[j]));
				smooth += (velocity[j] - velocity[i]) * (Kernel(predicted[i] - predicted[j]) / shared);
			}
			V v = velocity[i] + smooth * visc;
			p.touching = contact[i] >= 0 && p.canal < 0;
			if (p.canal >= 0) {
				const V carrier = CanalCarrier(a_canals[p.canal], predicted[i]);
				v = carrier + (v - carrier) * (1.0f / (1.0f + a_params.canalDrag * a_dt));
				p.surface = -1;
			} else if (p.touching) {
				const V surf = Carrier(a_capsules, contact[i], along[i]);
				const V rel = v - surf;
				const float nv = Dot(rel, normal[i]);
				V slide = rel - normal[i] * nv;
				const float slip = Len(slide);
				const float grip = a_params.grip * a_dt;
				slide = slip > grip ? slide * ((slip - grip) / slip / (1.0f + SkinDrag(a_params, weights[i]) * a_dt)) : V(0, 0, 0);
				v = surf + slide + normal[i] * std::clamp(nv, 0.0f, kSeparation);
				p.carried = surf;
				p.surface = contact[i];
				p.along = along[i];
			} else
				p.surface = -1;
			if (predicted[i].z <= lift + h * 0.01f) {
				v.z = (std::max)(v.z, 0.0f);
				v.x *= floorKeep;
				v.y *= floorKeep;
			}
			p.pos = predicted[i];
			p.vel = v;
			p.stuck = p.touching ? p.stuck + a_dt : 0.0f;
			if (anchored[i] || p.touching || p.canal >= 0 || Len2(v) > kSleepSpeed * kSleepSpeed)
				p.idle = 0.0f;
			else if ((p.idle += a_dt) > kSleepTime)
				p.asleep = true, p.vel = V(0, 0, 0);
		}

		// slime strings (10-17, the owner: "all sex fluids are stringy"): a particle that lay on a body a moment and left
		// it stays tied to the spot it left, by a string that stretches like dough (a_params.slime) and lets go long; a
		// particle back on a body drops its string. Drawn as a rope from the spot (Fluid.cpp: an anchored run)
		int strings = 0;
		for (Anchor& a : a_anchors)
			if (a.surface >= 0 && a.rest > 0.0f) {
				if (a.particle < n && a_particles[a.particle].stuck > kStringLanded)   // back on a body a while, not a brush
					a.rest = 0.0f;
				else
					strings++;
			}
		for (std::size_t i = 0; i < n && strings < a_params.strings; i++) {
			const Particle& p = a_particles[i];
			const int s = lastSurface[i];
			if (p.touching || s < 0 || (std::size_t)s >= a_capsules.size() || anchored[i] || lastStuck[i] < kStringAfter)
				continue;
			const Capsule& c = a_capsules[s];
			const V axis = c.a + (c.b - c.a) * lastAlong[i];
			const V point = axis + Unit(p.pos - axis, V(0, 0, 1)) * c.r;
			a_anchors.push_back(Anchor{ i, point, (std::max)(Len(p.pos - point), 0.5f * h), kSkinString, s, lastAlong[i] });
			anchored[i] = 1;
			strings++;
		}
	}

	// ---- clusters, pieces, runs

	std::vector<Cluster> FindClusters(const std::vector<Particle>& a_particles, const std::vector<std::size_t>& a_subset, float a_link)
	{
		std::vector<V> pts;
		pts.reserve(a_subset.size());
		for (std::size_t i : a_subset)
			pts.push_back(a_particles[i].pos);
		Grid g;
		g.Build(pts, a_link);
		std::vector<std::size_t> parent(pts.size());
		std::iota(parent.begin(), parent.end(), (std::size_t)0);
		auto find = [&](std::size_t x) {
			while (parent[x] != x)
				x = parent[x] = parent[parent[x]];
			return x;
		};
		const float l2 = a_link * a_link;
		for (std::size_t i = 0; i < pts.size(); i++)
			g.Near(pts[i], [&](std::size_t j) {
				if (j > i && Len2(pts[i] - pts[j]) < l2)
					parent[find(i)] = find(j);
			});
		std::unordered_map<std::size_t, std::size_t> index;
		std::vector<Cluster> out;
		for (std::size_t i = 0; i < pts.size(); i++) {
			const std::size_t r = find(i);
			auto it = index.find(r);
			if (it == index.end())
				it = index.emplace(r, out.size()).first, out.emplace_back();
			out[it->second].members.push_back(a_subset[i]);
		}
		for (Cluster& c : out) {
			V sum(0, 0, 0);
			for (std::size_t m : c.members)
				sum += a_particles[m].pos;
			c.center = sum * (1.0f / (float)c.members.size());
			float best = 1e30f;
			for (std::size_t m : c.members) {
				const float d = Len2(a_particles[m].pos - c.center);
				c.extent = (std::max)(c.extent, std::sqrt(d));
				if (d < best)
					best = d, c.seed = m;
			}
		}
		return out;
	}

	std::vector<Piece> SplitCluster(const std::vector<Particle>& a_particles, const Cluster& a_cluster, float a_radius,
		const std::vector<std::size_t>& a_hints)
	{
		// seeds: the hints still in the cluster (a blob keeps its place), then the member farthest from every seed while
		// it is beyond 1.5 radii (at most 8 pieces); each member goes to its nearest seed
		std::vector<std::size_t> seeds, hintOf;
		for (std::size_t h = 0; h < a_hints.size(); h++)
			if (std::find(a_cluster.members.begin(), a_cluster.members.end(), a_hints[h]) != a_cluster.members.end())
				seeds.push_back(a_hints[h]), hintOf.push_back(h);
		if (seeds.empty())
			seeds.push_back(a_cluster.seed), hintOf.push_back(kNone);
		if (a_cluster.extent > a_radius)
			while (seeds.size() < 8) {
				float far = 0.0f;
				std::size_t pick = kNone;
				for (std::size_t m : a_cluster.members) {
					float d = 1e30f;
					for (std::size_t s : seeds)
						d = (std::min)(d, Len2(a_particles[m].pos - a_particles[s].pos));
					if (d > far)
						far = d, pick = m;
				}
				if (pick == kNone || std::sqrt(far) < 1.5f * a_radius)
					break;
				seeds.push_back(pick);
				hintOf.push_back(kNone);
			}
		std::vector<Piece> out(seeds.size());
		std::vector<V> sums(seeds.size(), V(0, 0, 0));
		std::vector<std::size_t> owner(a_cluster.members.size());
		for (std::size_t k = 0; k < a_cluster.members.size(); k++) {
			const V& p = a_particles[a_cluster.members[k]].pos;
			std::size_t best = 0;
			float bd = 1e30f;
			for (std::size_t s = 0; s < seeds.size(); s++) {
				const float d = Len2(p - a_particles[seeds[s]].pos);
				if (d < bd)
					bd = d, best = s;
			}
			owner[k] = best;
			sums[best] += p;
			out[best].count++;
		}
		for (std::size_t s = 0; s < seeds.size(); s++) {
			out[s].seed = seeds[s];
			out[s].hint = hintOf[s];
			out[s].center = out[s].count ? sums[s] * (1.0f / (float)out[s].count) : a_particles[seeds[s]].pos;
		}
		for (std::size_t k = 0; k < a_cluster.members.size(); k++) {
			Piece& pc = out[owner[k]];
			pc.extent = (std::max)(pc.extent, Len(a_particles[a_cluster.members[k]].pos - pc.center));
		}
		std::erase_if(out, [](const Piece& p) { return p.count == 0; });
		return out;
	}

	std::vector<Run> FindRuns(const std::vector<Particle>& a_particles)
	{
		std::vector<Run> out;
		std::size_t begin = 0;
		for (std::size_t i = 0; i < a_particles.size(); i++)
			if (a_particles[i].link <= 0.0f) {
				out.push_back(Run{ begin, i + 1 });
				begin = i + 1;
			}
		return out;
	}

	float VolumeRadius(std::size_t a_count, float a_volume)
	{
		return std::cbrt(3.0f * (float)a_count * a_volume / (4.0f * 3.14159265f));
	}

	// ---- the field

	Density::Density(const std::vector<Particle>& a_particles, float a_radius, float a_fade) :
		particles(a_particles), r(a_radius), r2(a_radius * a_radius), fade((std::max)(a_fade, 1e-3f))
	{
		std::vector<V> pts;
		pts.reserve(a_particles.size());
		for (const Particle& p : a_particles)
			pts.push_back(p.pos);
		grid.Build(pts, a_radius);
	}

	float Density::K(const V& d) const
	{
		const float q = (std::max)(0.0f, 1.0f - Len2(d) / r2);
		return q * q * q;
	}

	float Density::Weight(const Particle& a_p) const { return std::clamp(a_p.life / fade, 0.0f, 1.0f); }

	float Density::Value(const V& p) const
	{
		float s = 0.0f;
		grid.Near(p, [&](std::size_t j) { s += K(p - particles[j].pos) * Weight(particles[j]); });
		return s;
	}

	float Density::Count(const V& p) const
	{
		float s = 0.0f;
		grid.Near(p, [&](std::size_t j) { s += K(p - particles[j].pos); });
		return s;
	}

	float Density::BulkCount(const V& p) const
	{
		float s = 0.0f;
		grid.Near(p, [&](std::size_t j) {
			if (particles[j].bulk)
				s += K(p - particles[j].pos);
		});
		return s;
	}

	V Density::Normal(const V& p, const V& a_fallback) const
	{
		V g(0, 0, 0);   // minus the field's gradient: outward
		grid.Near(p, [&](std::size_t j) {
			const V d = p - particles[j].pos;
			const float q = (std::max)(0.0f, 1.0f - Len2(d) / r2);
			g += d * (6.0f * q * q / r2 * Weight(particles[j]));
		});
		return Unit(g, a_fallback);
	}

	bool Drape(const Density& a_field, const std::vector<Particle>& a_particles, const std::vector<std::size_t>& a_members,
		const std::vector<Capsule>& a_capsules, int a_n, float a_threshold, float a_margin, float a_sink, std::vector<SurfacePoint>& a_out,
		float a_flat)
	{
		if (a_members.empty() || a_n < 2)
			return false;
		std::vector<int> votes(a_capsules.size(), 0);
		V mid(0, 0, 0);
		for (std::size_t m : a_members) {
			const Particle& p = a_particles[m];
			mid += p.pos;
			if (p.surface >= 0 && (std::size_t)p.surface < a_capsules.size())
				votes[p.surface]++;
		}
		mid = mid * (1.0f / (float)a_members.size());
		const auto best = std::max_element(votes.begin(), votes.end());
		if (best == votes.end() || *best == 0)
			return false;
		const Capsule& c = a_capsules[best - votes.begin()];
		const V span = c.b - c.a;
		const float l2 = Len2(span);
		auto axisPoint = [&](const V& q) {
			const float t = l2 > 1e-12f ? std::clamp(Dot(q - c.a, span) / l2, 0.0f, 1.0f) : 0.0f;
			return c.a + span * t;
		};
		const V n = Unit(mid - axisPoint(mid), V(0, 0, 1));
		// the footprint's longest spread: the principal axis of the members in the plane across n
		V u0 = Unit(V(0, 0, 1) - n * n.z, V(1, 0, 0));
		if (std::fabs(n.z) > 0.9f)
			u0 = Unit(V(1, 0, 0) - n * n.x, V(0, 1, 0));
		const V v0(n.y * u0.z - n.z * u0.y, n.z * u0.x - n.x * u0.z, n.x * u0.y - n.y * u0.x);
		float sxx = 0.0f, syy = 0.0f, sxy = 0.0f;
		for (std::size_t m : a_members) {
			const V d = a_particles[m].pos - mid;
			const float x = Dot(d, u0), y = Dot(d, v0);
			sxx += x * x, syy += y * y, sxy += x * y;
		}
		const float th = 0.5f * std::atan2(2.0f * sxy, sxx - syy);
		const V u = u0 * std::cos(th) + v0 * std::sin(th);
		const V v(n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x);
		float hu = 0.0f, hv = 0.0f;
		for (std::size_t m : a_members) {
			const V d = a_particles[m].pos - mid;
			hu = (std::max)(hu, std::fabs(Dot(d, u))), hv = (std::max)(hv, std::fabs(Dot(d, v)));
		}
		hu += a_margin, hv += a_margin;
		const float h = a_field.Radius() * 0.5f;   // the field's radius is two spacings
		a_out.resize((std::size_t)a_n * a_n);
		bool any = false;
		for (int j = 0; j < a_n; j++)
			for (int i = 0; i < a_n; i++) {
				const float x = -hu + 2.0f * hu * i / (a_n - 1), y = -hv + 2.0f * hv * j / (a_n - 1);
				const V g = mid + u * x + v * y;
				const V on = axisPoint(g);
				const V radial = Unit(g - on, n);
				const V skin = on + radial * c.r;
				SurfacePoint& sp = a_out[(std::size_t)j * a_n + i];
				float inside = 0.0f, outside = -1.0f;
				for (float t = 0.0f; t <= 5.0f * h; t += 0.5f * h) {   // up from the skin, through the fluid (its centres
					if (a_field.Value(skin + radial * t) >= a_threshold) // sit half a spacing up)
						inside = t, outside = -1.0f;
					else if (inside > 0.0f || t >= 1.0f * h) {
						outside = t;
						if (inside > 0.0f)
							break;
					}
				}
				if (inside <= 0.0f && a_field.Value(skin + radial * (0.5f * h)) < a_threshold) {
					sp.pos = skin - radial * a_sink;
					sp.normal = radial;
					sp.dist = 0.0f;
					continue;
				}
				if (outside < 0.0f)
					outside = inside + 0.5f * h;
				for (int b = 0; b < 4; b++) {
					const float half = 0.5f * (inside + outside);
					if (a_field.Value(skin + radial * half) < a_threshold)
						outside = half;
					else
						inside = half;
				}
				const float t = 0.5f * (inside + outside);
				sp.pos = skin + radial * t;
				sp.normal = Unit(a_field.Normal(sp.pos, radial) + radial, radial);   // half the skin's: a smear's top is flat
				sp.pos = skin + radial * (t * a_flat);
				sp.dist = t;
				any = true;
			}
		return any;
	}

	bool Wrap(const Density& a_field, const V& a_center, float a_reach, float a_threshold, const std::vector<V>& a_dirs,
		std::vector<SurfacePoint>& a_out)
	{
		if (a_field.Value(a_center) < a_threshold)
			return false;
		a_out.resize(a_dirs.size());
		constexpr int kSteps = 16;
		const float step = a_reach / kSteps;
		for (std::size_t k = 0; k < a_dirs.size(); k++) {
			const V& d = a_dirs[k];
			float inside = 0.0f, outside = a_reach;
			for (int s = 1; s <= kSteps; s++) {
				const float t = step * s;
				if (a_field.Value(a_center + d * t) < a_threshold) {
					outside = t;
					break;
				}
				inside = t;
			}
			for (int b = 0; b < 4; b++) {   // the crossing, by halves
				const float mid = 0.5f * (inside + outside);
				if (a_field.Value(a_center + d * mid) < a_threshold)
					outside = mid;
				else
					inside = mid;
			}
			const float t = 0.5f * (inside + outside);
			a_out[k].pos = a_center + d * t;
			a_out[k].normal = a_field.Normal(a_out[k].pos, d);
			a_out[k].dist = t;
		}
		return true;
	}
}
