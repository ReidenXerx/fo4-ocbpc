// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: the penis finds the opening it is meant for (A-28).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
//
// Pure: no engine type, so it is tested outside the game (tests/aim). Aim.cpp gathers the chains, the
// openings and the hands from the running game, calls Update once a frame per chain, and writes the result.
//
// A chain is a penis: bones from a root to a tip, each hanging from the last. An opening is a point on
// someone's body, the direction INTO it, and the path inside (her body's middle, measured; a throat).
// A hand gripping the shaft is an opening too (the centre of its fingers, along its knuckles), and while
// one grips it is the ONLY one: the shaft runs through the grip instead of into anyone, and for a moment
// after the hand lets go into no one. Each frame a chain keeps the opening it is locked on while it still
// fits loosely, or locks on the one the animation came closest to entering. Locked, the shaft runs
// straight from its root to the entrance and then bends bone by bone along the path inside, like a snake
// into its hole, so a deep or angled thrust stays inside her instead of coming out through her. The chain
// stretches a little when it falls short (never for a hand). Each bone's turn is a correction ON TOP of
// the animation's pose, in that bone's parent's frame, smoothed in and out. Nothing is forced: an opening
// the animation misses by far, one entered from the side or from inside, or out of reach, is left to the
// animation.
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
		kHand = 3,        // a gripping hand: the shaft through its grip, either way along it
	};
	const char* KindName(int kind);

	struct Target
	{
		std::uint32_t owner = 0;
		int kind = kVagina;
		V3 point;                 // the entrance, world
		V3 in;                    // unit, pointing INTO the body
		std::vector<V3> path;     // inside, world, from the entrance on; empty: straight along `in`
		bool inScene = false;
	};

	// A hand that may be holding a shaft (a knuckle bone), anyone's.
	struct Hand
	{
		std::uint32_t owner = 0;
		V3 point;
	};

	// The chain as the animation posed it. Joint i hangs from joint i-1 at offsets[i] in i-1's frame
	// (world units: scale already in). World rotation of joint i = parent x locals[0] x ... x locals[i].
	struct Chain
	{
		std::uint32_t owner = 0;
		bool inScene = false;
		Quat parent;                  // the root's parent's world rotation
		V3 root;                      // the root joint's world position
		std::vector<Quat> locals;     // n: each joint's local rotation
		std::vector<V3> offsets;      // n: offsets[0] unused, offsets[i] = joint i in joint i-1's frame
	};
	// World joints and rotations of a chain with these locals and offsets x stretch (1 for the animation's).
	void Pose(const Chain& c, const std::vector<Quat>& locals, float stretch, std::vector<V3>& joints,
		std::vector<Quat>& world);
	float ChainLength(const Chain& c);

	struct Params
	{
		float captureAngle = 0.61f;   // radians (35 degrees): the most a new lock may turn the shaft
		float keepAngle = 0.79f;      // (45): a lock is kept up to here, so it does not flicker at the edge
		float captureMiss = 5.0f;     // units: the animation's shaft line passes at most this far from the
		float keepMiss = 8.0f;        //   opening to lock on it (to keep it): a near miss, not another act
		float entryAngle = 1.31f;     // (75): the shaft must run within this of the opening's inward axis
		float reach = 1.3f;           // the opening's centre at most this x the chain's length from its root
		float minReach = 2.0f;        // ... and at least this far (units)
		float depth = 2.0f;           // the angle is judged toward a point this far inside
		float minInside = 3.0f;       // the stretch lets at least this much of the shaft pass the entrance
		float maxStretch = 1.10f;     // ... up to this x its length
		float rate = 8.0f;            // 1/s: how fast a lock fades in and out (and one opening hands to the next)
		float handRadius = 4.0f;      // a knuckle this close to the shaft's outer part: a hand holds it
		float handHold = 1.0f;        // seconds a held shaft stays unaimed after the hand lets go
		bool requireScene = true;     // both the chain's owner and the opening's in a scene
	};

	// While locked, the bend is computed EXACTLY every frame: a correction that chased it at a fixed pace
	// lagged behind a moving head, and on a fast pull-out the glans went through her chin (the owner's
	// look, 2026-09-24). Only the lock's weight is smoothed (in and out), and a switch from one opening to
	// another crossfades from the pose that was showing.
	struct State
	{
		bool locked = false;
		std::uint32_t lockedOwner = 0;
		int lockedKind = -1;
		float weight = 0.0f;          // 0..1: how much of the bend is on
		std::vector<Quat> want;       // the last exact bend (a released lock fades out from it)
		float wantStretch = 1.0f;
		std::vector<Quat> from;       // the pose showing when the opening changed, fading out
		float fromStretch = 1.0f;
		float fromFade = 0.0f;
		std::vector<Quat> correction; // what was applied last frame, per joint but the tip
		float stretch = 1.0f;
		float clock = 0.0f;           // seconds this state has run
		float heldUntil = -1.0f;      // a hand held the shaft: no aim before this
	};

	struct Result
	{
		std::vector<Quat> local;      // per joint but the tip: its local rotation becomes local[i] x locals[i]
		float stretch = 1.0f;         // the offsets of joints 1.. x this
		bool active = false;          // anything to write (a lock, or one still fading out)
		bool locked = false;
		bool newLock = false;         // locked this frame (a new opening)
		bool held = false;            // a hand holds the shaft this frame
		bool released = false;        // the lock let go this frame, for `why`
		const char* why = "";
		std::uint32_t targetOwner = 0;
		int targetKind = -1;
		float angle = 0.0f;           // radians the lock asks of the root (0 without one)
		float miss = 0.0f;            // how far the animation's shaft line missed the entrance
	};

	// Would this chain enter this opening? keep: judged as a lock already held.
	struct Fit
	{
		bool ok = false;
		float angle = 0.0f;
		float miss = 0.0f;
		float stretch = 1.0f;
		const char* why = "";         // why not, when not ok
	};
	Fit Judge(const Chain& c, const std::vector<V3>& joints, const Target& t, const Params& p, bool keep);
	bool Held(const Chain& c, const std::vector<V3>& joints, const std::vector<Hand>& hands, const Params& p);

	// The middle of a gripping hand, from its four fingers' three joints each (index to little, knuckle
	// out). A curled finger's joints lie on a circle around what it holds, so the grip is the centre of
	// that circle, not the joints' average, which sits in the fingers: a shaft aimed there ran outside
	// the grip, along the fingers (the owner's look, 2026-09-25). A finger that does not curl (a circle
	// wider than maxRadius, or joints in line) is skipped; with fewer than two curled, no grip.
	bool GripCentre(const V3 joints[4][3], float maxRadius, V3& centre);

	// The corrections that lay the chain along root -> entrance -> path, joint by joint (no smoothing).
	std::vector<Quat> Bend(const Chain& c, const Target& t, float stretch);

	Result Update(State& s, const Chain& c, const std::vector<Target>& targets, const std::vector<Hand>& hands,
		const Params& p, float dt);
}
