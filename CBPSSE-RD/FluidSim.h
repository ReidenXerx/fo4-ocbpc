// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16: the climax fluid's particles (Fluid.h draws them).
// The method is position-based fluids (Macklin and Mueller, 2013) as OStrands uses it for its fluid
// (https://gitlab.com/LilRicefield/ostrands, src/fluid, GPL-3, by LilRicefield): its constants and its choices
// (cohesion, sticky contacts with grip, linked runs that stretch and snap, anchors, sleep) are followed here.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// Everything here is game-free: metres, a frame of its own (Fluid.cpp's domain: origin on the floor, z up), the floor
// the plane z = 0. One Step moves every particle by dt: cohesion and gravity predict, the density constraint keeps them
// a spacing apart, consecutive particles of one run hold together as a thread (link: the rest length to the next one,
// 0: none) and stretch and snap, anchors hold a run's newest particle to its source, capsules (the bodies) push out
// and, where a particle touches, carry it and hold it with grip and friction.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "NiMath.h"   // only NiPoint3: the solver is game-free (tests/fluid builds it alone)

namespace FluidSim
{
	using V = NiPoint3;
	constexpr std::size_t kNone = (std::size_t)-1;

	struct Particle
	{
		V pos, vel;
		V carried;                 // the velocity of the surface it rode last step
		float life = 0.0f;         // seconds left
		float link = 0.0f;         // rest length to the next particle of its run (0: not linked)
		float idle = 0.0f;
		float stuck = 0.0f;        // seconds it has touched a body without a break
		float along = 0.0f;        // where on its capsule it touched (0 .. 1, a to b)
		int surface = -1;          // the capsule it touches (-1: none)
		int canal = -1;            // the canal it is inside (-1: none)
		std::uint32_t run = 0;     // the pulse or glob it came out in
		bool bulk = false, asleep = false, touching = false;
	};

	struct Capsule
	{
		V a, b;
		float r = 0.0f;
		V va, vb;                  // the velocities of its two ends
		bool shaft = false;        // his shaft: it pushes fluid inside a canal too (a body's capsules do not)
	};
	struct Canal                   // the inside of an opening: fluid in it stays in, but for the open entrance
	{
		V a, b;                    // a: the entrance (open), b: the deep end (closed)
		float r = 0.0f;
		V va, vb;
	};

	struct Hole                    // capsules do not push in here: an opening, his tip
	{
		V c;
		float r = 0.0f;
	};

	struct Anchor                  // a run's newest particle held to where it came out
	{
		std::size_t particle = 0;
		V point;
		float rest = 0.0f;         // 0: let go
		std::uint32_t owner = 0;
		int surface = -1;          // a slime string from a body (kSkinString): the capsule it hangs from, and where on it;
		float along = 0.0f;        // the point rides that capsule
	};

	struct Params
	{
		int iterations = 4;
		float cohesion = 15.0f;
		float viscosity = 50.0f;
		float gravity = 9.81f;
		float stretch = 5.0f;      // how fast a stretched link's rest follows its length (per second)
		float friction = 60.0f;    // sliding on skin, lost per second (OStrands 30; 60 holds a landed shot, tests/fluid)
		float grip = 0.5f;         // static friction: holds while the push along the skin is under grip (m/s^2): on our
		                           // spheres up to sin(angle) = grip / (0.25 g), 2.5 about the equator (OStrands 0.4)
		float film = 20.0f;        // a thin film drags more (10-17, "sticky liquid, not a gel"): friction x (1 + film x
		                           // thin^2), thin 1 for a lone particle, 0 for one inside a glob. A film stays, a glob runs
		int strings = 24;          // slime strings at once: a particle leaving a body it lay on stays tied to it
		float slime = 3.0f;        // how fast a slime string gives way (per second; the stream's own is 15): lower, longer
		float canalDrag = 3000.0f;  // flow along a canal's walls, lost per second: what is left inside seeps out slowly
		float skinCohesion = 0.2f;   // cohesion on a body, of the air's: a running glob leaves its thin edge behind
	};

	// a hash grid of points, cell = the search radius
	class Grid
	{
	public:
		void Build(const std::vector<V>& a_points, float a_cell);
		template <class F>
		void Near(const V& a_point, F&& a_visit) const
		{
			const int cx = Cell(a_point.x), cy = Cell(a_point.y), cz = Cell(a_point.z);
			for (int z = cz - 1; z <= cz + 1; z++)
				for (int y = cy - 1; y <= cy + 1; y++)
					for (int x = cx - 1; x <= cx + 1; x++) {
						const auto it = cells.find(Key(x, y, z));
						if (it != cells.end())
							for (std::size_t j : it->second)
								a_visit(j);
					}
		}

	private:
		int Cell(float a_v) const { return (int)std::floor(a_v / cell); }
		static std::uint64_t Key(int x, int y, int z)
		{
			return ((std::uint64_t)(std::uint32_t)(x & 0x1FFFFF) << 42) | ((std::uint64_t)(std::uint32_t)(y & 0x1FFFFF) << 21) |
			       (std::uint64_t)(std::uint32_t)(z & 0x1FFFFF);
		}
		float cell = 1.0f;
		std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells;
	};

	constexpr std::uint32_t kSkinString = 0xFFFF0000u;   // the owner of a slime string's Anchor

	class Solver
	{
	public:
		explicit Solver(float a_spacing);
		void Step(std::vector<Particle>& a_particles, std::vector<Anchor>& a_anchors, const std::vector<Capsule>& a_capsules,
			const std::vector<Hole>& a_holes, const Params& a_params, float a_dt, const std::vector<Canal>& a_canals = {});
		float Spacing() const { return h; }

	private:
		float Kernel(const V& d) const;        // (1 - r^2 / R^2)^3 inside R = 2h
		V KernelGradient(const V& d) const;
		void Neighbours(const std::vector<Particle>& a_particles, bool a_all);
		void Collide(const std::vector<Particle>& a_particles, const std::vector<Capsule>& a_capsules, const std::vector<Hole>& a_holes,
			bool a_pull);
		static V Carrier(const std::vector<Capsule>& a_capsules, int a_surface, float a_along);
		// a canal keeps its particles (and takes in one at its entrance when a_join): inside its radius, short of its end
		void Contain(std::vector<Particle>& a_particles, const std::vector<Canal>& a_canals, bool a_join);
		static V CanalCarrier(const Canal& a_canal, const V& a_point);
		float SkinDrag(const Params& a_params, float a_crowd) const;   // sliding friction on a body, more as it thins

		float h, R2, rest = 0.0f;
		Grid grid;
		std::vector<V> predicted, velocity, correction, normal;
		std::vector<float> lambda, weights, along;
		std::vector<int> contact, lastSurface;
		std::vector<float> lastAlong, lastStuck, squeeze;
		std::vector<char> anchored, pushed;
		std::vector<std::vector<std::size_t>> near;
	};

	// ---- what Fluid.cpp draws from them

	struct Cluster
	{
		V center;
		std::size_t seed = 0;      // the member nearest the centre
		float extent = 0.0f;       // the farthest member from the centre
		std::vector<std::size_t> members;
	};

	struct Piece
	{
		std::size_t seed = 0;
		std::size_t hint = kNone;  // which of the hints (a blob's last seed) it keeps
		V center;
		float extent = 0.0f;
		std::size_t count = 0;
	};

	struct Run                     // [begin, end): consecutive particles linked one to the next
	{
		std::size_t begin = 0, end = 0;
		std::size_t Size() const { return end - begin; }
	};

	std::vector<Cluster> FindClusters(const std::vector<Particle>& a_particles, const std::vector<std::size_t>& a_subset, float a_link);
	std::vector<Piece> SplitCluster(const std::vector<Particle>& a_particles, const Cluster& a_cluster, float a_radius,
		const std::vector<std::size_t>& a_hints);
	std::vector<Run> FindRuns(const std::vector<Particle>& a_particles);
	float VolumeRadius(std::size_t a_count, float a_volume);   // the sphere that holds a_count particles' volume

	// the particles as a smooth field: each one a kernel of radius R, weighted by how faded it is
	class Density
	{
	public:
		Density(const std::vector<Particle>& a_particles, float a_radius, float a_fade);
		float Value(const V& p) const;       // weighted
		float Count(const V& p) const;       // unweighted: how many are around (the bulk test)
		float BulkCount(const V& p) const;   // only bulk ones
		V Normal(const V& p, const V& a_fallback) const;
		float Weight(const Particle& a_p) const;
		float Radius() const { return r; }

	private:
		float K(const V& d) const;
		const std::vector<Particle>& particles;
		float r, r2, fade;
		Grid grid;
	};

	struct SurfacePoint
	{
		V pos, normal;
		float dist = 0.0f;
	};

	// a blob's skin: along each direction from a_center, where the field falls under a_threshold (a_reach at most).
	// False when a_center is not inside the field at all
	bool Wrap(const Density& a_field, const V& a_center, float a_reach, float a_threshold, const std::vector<V>& a_dirs,
		std::vector<SurfacePoint>& a_out);

	// a smear on a body as one sheet: an a_n x a_n grid (row by row) over a_members' footprint on the capsule most of
	// them touch, laid along their longest spread, a_margin past them. Each point sits on that capsule's surface, lifted
	// along its normal to where the field falls under a_threshold, or a_sink under the skin where there is no fluid (the
	// sheet's edge goes under it). a_out[k].dist: the fluid's height there (0: none). False: no capsule, or no fluid
	bool Drape(const Density& a_field, const std::vector<Particle>& a_particles, const std::vector<std::size_t>& a_members,
		const std::vector<Capsule>& a_capsules, int a_n, float a_threshold, float a_margin, float a_sink, std::vector<SurfacePoint>& a_out,
		float a_flat = 1.0f);   // a_flat: the share of the fluid's height the points are lifted (draw only)
}
