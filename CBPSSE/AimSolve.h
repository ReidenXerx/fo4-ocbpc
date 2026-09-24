// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: the penis finds the opening it is meant for (A-28).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
//
// Pure: no engine type, so it is tested outside the game (tests/aim). Aim.cpp gathers the chains and
// the openings from the running game, calls Update once a frame per chain, and writes the result.
//
// A chain is a penis: a root bone the whole shaft hangs from, and its tip. An opening (a target) is a
// point on someone's body and the direction INTO it. Each frame a chain keeps the opening it is locked
// on while it stays within keepAngle, or locks on the closest one within captureAngle, and turns the
// shaft about its root so it runs into that opening. The turn is a correction ON TOP of the animation's
// pose, in the root's parent's frame (so it rides along when the actor turns), smoothed in and out.
// Nothing is ever forced: an opening past the angles, behind the shaft, entered from inside or out of
// reach is left to the animation.
#pragma once

#include <cstdint>
#include <vector>

namespace AimSolve
{
	struct V3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;
	};
	V3 Add(const V3& a, const V3& b);
	V3 Sub(const V3& a, const V3& b);
	V3 Scale(const V3& a, float s);
	float Dot(const V3& a, const V3& b);
	V3 Cross(const V3& a, const V3& b);
	float Length(const V3& a);
	V3 Normalized(const V3& a);

	// A rotation as a unit quaternion; Rotate(q, v) turns v by it.
	struct Quat
	{
		float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
	};
	Quat Mul(const Quat& a, const Quat& b);   // a after b
	Quat Conj(const Quat& q);
	V3 Rotate(const Quat& q, const V3& v);
	Quat FromTo(const V3& from, const V3& to);   // the shortest turn taking one unit vector onto another
	Quat Slerp(Quat a, const Quat& b, float t);
	float Angle(const Quat& q);                  // radians turned, 0..pi

	// A rotation matrix, row-major: v' = m * v (columns are the frame's axes in the outer frame).
	struct M3
	{
		float m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	};
	M3 ToMatrix(const Quat& q);
	Quat FromMatrix(const M3& r);

	enum Kind
	{
		kVagina = 0,
		kAnus = 1,
		kMouth = 2,
	};
	const char* KindName(int kind);

	struct Target
	{
		std::uint32_t owner = 0;
		int kind = kVagina;
		V3 point;          // the opening's centre, world
		V3 in;             // unit, pointing INTO the body
		bool inScene = false;
	};

	struct Chain
	{
		std::uint32_t owner = 0;
		bool inScene = false;
		V3 base;           // the root bone's origin, world: the shaft turns about it
		V3 tip;            // the last bone's origin, world, as the animation put it
		float length = 0;  // base to tip along the bones
		Quat parent;       // the root's parent's world rotation
	};

	struct Params
	{
		float captureAngle = 0.61f;   // radians (35 degrees): the most a new lock may turn the shaft
		float keepAngle = 0.79f;      // (45): a lock is kept up to here, so it does not flicker at the edge
		float entryAngle = 1.31f;     // (75): the shaft must run within this of the opening's inward axis
		float reach = 1.3f;           // the opening's centre at most this x the chain's length from its root
		float minReach = 2.0f;        // ... and at least this far (units)
		float depth = 2.0f;           // aim this far inside the opening, so the shaft follows it in
		float minInside = 3.0f;       // the stretch lets at least this much of the shaft pass the entrance
		float maxStretch = 1.10f;     // ... up to this x its length
		float rate = 8.0f;            // 1/s: how fast the correction follows (in and out)
		bool requireScene = true;     // both the chain's owner and the opening's in a scene
	};

	struct State
	{
		bool locked = false;
		std::uint32_t lockedOwner = 0;
		int lockedKind = -1;
		Quat correction;              // in the root's parent's frame, smoothed
		float stretch = 1.0f;         // smoothed
	};

	struct Result
	{
		Quat local;                   // the correction to put on the root's local rotation (parent frame)
		float stretch = 1.0f;         // the children's offsets x this
		bool active = false;          // anything to write (a lock, or one still fading out)
		bool locked = false;
		bool newLock = false;         // locked this frame (a new opening)
		std::uint32_t targetOwner = 0;
		int targetKind = -1;
		float angle = 0.0f;           // radians the lock asks for (0 without one)
	};

	// Would this chain enter this opening, and with what turn? keep: judged as a lock already held.
	struct Fit
	{
		bool ok = false;
		float angle = 0.0f;
		Quat world;                   // the turn, world frame
		float stretch = 1.0f;
	};
	Fit Judge(const Chain& c, const Target& t, const Params& p, bool keep);

	Result Update(State& s, const Chain& c, const std::vector<Target>& targets, const Params& p, float dt);
}
