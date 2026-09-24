// fo4-ocbpc: the penis finds its opening (A-28), tested outside the game. Build and run: tests/aim/run.bat.
// Each case states what it proves; the process exits 1 if any expectation fails.
#include "AimSolve.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace AimSolve;

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}
static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static const float kDeg = 57.29578f;
static const float kStep = 1.0f / 60.0f;

// ZeX's shaft: 5 bones of 3.2 (16 long) from the origin along `dir`, straight, both in a scene.
static Chain Shaft(V3 dir = { 0, 1, 0 }, V3 root = { 0, 0, 0 })
{
	Chain c;
	c.owner = 1;
	c.inScene = true;
	c.parent = FromTo({ 0, 1, 0 }, dir);
	c.root = root;
	c.locals.assign(6, Quat{});
	c.offsets.assign(6, V3{ 0, 3.2f, 0 });
	c.offsets[0] = V3{};
	return c;
}

static Target Opening(std::uint32_t owner, int kind, V3 point, V3 in, bool inScene = true, std::vector<V3> path = {})
{
	Target t;
	t.owner = owner;
	t.kind = kind;
	t.point = point;
	t.in = Normalized(in);
	t.inScene = inScene;
	t.path = path;
	return t;
}

// The chain as written: local[i] x locals[i] for every joint but the tip, offsets x stretch.
static std::vector<V3> Written(const Chain& c, const Result& r)
{
	std::vector<Quat> locals = c.locals;
	for (size_t i = 0; i < r.local.size(); i++)
		locals[i] = Mul(r.local[i], c.locals[i]);
	std::vector<V3> joints;
	std::vector<Quat> world;
	Pose(c, locals, r.stretch, joints, world);
	return joints;
}

static Result Settle(State& s, const Chain& c, const std::vector<Target>& ts, const Params& p,
	const std::vector<Hand>& hands = {}, int frames = 240)
{
	Result r;
	for (int i = 0; i < frames; i++)
		r = Update(s, c, ts, hands, p, kStep);
	return r;
}

static float SegDist(V3 x, V3 a, V3 b)
{
	V3 ab = Sub(b, a);
	float t = Dot(Sub(x, a), ab) / Dot(ab, ab);
	t = t < 0 ? 0 : (t > 1 ? 1 : t);
	return Length(Sub(x, Add(a, Scale(ab, t))));
}
static float PolyDist(V3 x, const std::vector<V3>& line)
{
	float best = 1e9f;
	for (size_t k = 0; k + 1 < line.size(); k++)
		best = (std::min)(best, SegDist(x, line[k], line[k + 1]));
	return best;
}

int main()
{
	Params p;

	{   // 1. a vagina the animation misses by 1.2: locked; the settled shaft passes through the entrance and
		//    goes straight on inside along its axis (no path given)
		Chain c = Shaft();
		Target t = Opening(2, kVagina, { 0, 10, 1.2f }, { 0, 1, 0 });
		State s;
		Result r = Settle(s, c, { t }, p);
		Expect(r.locked && r.targetOwner == 2 && r.targetKind == kVagina, "1: locks on a vagina missed by 1.2");
		Expect(Near(r.miss, 1.2f, 0.01f), "1: the miss is the animation's line's distance from the entrance");
		std::vector<V3> j = Written(c, r);
		std::vector<V3> line{ c.root, t.point, Add(t.point, Scale(t.in, 24.0f)) };
		float worst = 0;
		for (auto& x : j)
			worst = (std::max)(worst, PolyDist(x, line));
		Expect(worst < 0.05f, "1: every joint lies on root -> entrance -> straight in");
	}
	{   // 2. the snake: a path inside that curves; the joints past the entrance follow it
		Chain c = Shaft();
		std::vector<V3> path{ { 0, 11, 0 }, { 0, 13, 1.5f }, { 0, 14.5f, 4 }, { 0, 15, 7 }, { 0, 15, 10 } };
		Target t = Opening(2, kVagina, { 0, 8, 0.5f }, { 0, 1, 0 }, true, path);
		State s;
		Result r = Settle(s, c, { t }, p);
		std::vector<V3> j = Written(c, r);
		std::vector<V3> line{ c.root, t.point };
		line.insert(line.end(), path.begin(), path.end());
		float worst = 0;
		for (auto& x : j)
			worst = (std::max)(worst, PolyDist(x, line));
		Expect(r.locked && worst < 0.01f, "2: every joint lies on the curved path (the shaft bends inside her)");
		Expect(j.back().z > 3.0f, "2: the tip follows the curve up, not straight out along the entrance axis");
		Expect(Near(Length(Sub(j[1], j[0])), 3.2f, 0.01f) && Near(Length(Sub(j[5], j[4])), 3.2f, 0.01f),
			"2: bending keeps every bone's length");
	}
	{   // 3. an opening 90 degrees off is left to the animation
		Chain c = Shaft();
		State s;
		Result r = Settle(s, c, { Opening(2, kVagina, { 12, 0, 0 }, { 1, 0, 0 }) }, p);
		Expect(!r.locked && !r.active, "3: 90 degrees off is never captured");
	}
	{   // 4. one's own opening is never a target
		Chain c = Shaft();
		State s;
		Expect(!Settle(s, c, { Opening(1, kMouth, { 0, 12, 0 }, { 0, 1, 0 }) }, p).locked, "4: never one's own mouth");
	}
	{   // 5. out of a scene: nothing, unless the scene is not required
		Chain c = Shaft();
		Target t = Opening(2, kVagina, { 0, 12, 1 }, { 0, 1, 0 }, false);
		State s;
		Expect(!Settle(s, c, { t }, p).locked, "5: an opening out of a scene is left alone");
		Params q = p;
		q.requireScene = false;
		State s2;
		Expect(Settle(s2, c, { t }, q).locked, "5: ...and taken when requireScene is off");
	}
	{   // 6. angle hysteresis (misses allowed wide): kept up to keepAngle, released past it
		Chain c = Shaft();
		auto at = [](float deg) { return V3{ 12 * std::sin(deg / kDeg), 12 * std::cos(deg / kDeg), 0 }; };
		Params q = p;
		q.depth = 0.0f;
		q.captureMiss = q.keepMiss = 100.0f;
		State s;
		Settle(s, c, { Opening(2, kVagina, at(30), at(30)) }, q);
		Expect(s.locked, "6: locks at 30 degrees");
		Expect(Update(s, c, { Opening(2, kVagina, at(40), at(40)) }, {}, q, kStep).locked, "6: kept at 40");
		Expect(!Update(s, c, { Opening(2, kVagina, at(50), at(50)) }, {}, q, kStep).locked, "6: released at 50");
		State fresh;
		Expect(!Update(fresh, c, { Opening(2, kVagina, at(40), at(40)) }, {}, q, kStep).locked,
			"6: a NEW lock at 40 is refused");
	}
	{   // 7. the near-miss gate: the animation's line must pass near the entrance (a hand job beside her
		//    at 30 degrees misses her by ~6: not captured); a held lock is kept to keepMiss
		Chain c = Shaft();
		State s;
		Expect(!Settle(s, c, { Opening(2, kVagina, { 6, 11, 0 }, { 0.5f, 1, 0 }) }, p).locked,
			"7: an entrance 6 beside the shaft's line is not captured");
		State s2;
		Settle(s2, c, { Opening(2, kVagina, { 3, 12, 0 }, { 0, 1, 0 }) }, p);
		Expect(s2.locked, "7: 3 beside: captured");
		Expect(Update(s2, c, { Opening(2, kVagina, { 7, 12, 0 }, { 0, 1, 0 }) }, {}, p, kStep).locked,
			"7: 7 beside: kept (within keepMiss)");
		Expect(!Update(s2, c, { Opening(2, kVagina, { 9, 12, 0 }, { 0, 1, 0 }) }, {}, p, kStep).locked,
			"7: 9 beside: released");
	}
	{   // 8. entered from inside, or from the side: refused
		Chain c = Shaft();
		State s;
		Expect(!Settle(s, c, { Opening(2, kAnus, { 0, 12, 0 }, { 0, -1, 0 }) }, p).locked, "8: not from inside");
		State s2;
		Expect(!Settle(s2, c, { Opening(2, kAnus, { 0, 12, 0 }, { 1, 0, 0 }) }, p).locked, "8: nor from the side");
	}
	{   // 9. too close / too far
		Chain c = Shaft();
		State s;
		Expect(!Settle(s, c, { Opening(2, kVagina, { 0, 1, 0 }, { 0, 1, 0 }) }, p).locked, "9: closer than minReach");
		State s2;
		Expect(!Settle(s2, c, { Opening(2, kVagina, { 0, 22, 0 }, { 0, 1, 0 }) }, p).locked, "9: past reach");
	}
	{   // 10. the stretch: enough for minInside past the entrance, capped, never shortened
		Chain c = Shaft();
		State s;
		Result r = Settle(s, c, { Opening(2, kVagina, { 0, 14, 0 }, { 0, 1, 0 }) }, p);
		Expect(r.locked && Near(r.stretch, 17.0f / 16.0f, 0.01f), "10: 14 away -> (14 + 3) / 16");
		State s2;
		std::vector<V3> curve{ { 0, 18.5f, 1 }, { 0, 19.5f, 3 }, { 0, 20, 6 }, { 0, 20, 9 } };
		Target far = Opening(2, kVagina, { 0, 17, 1 }, { 0, 1, 0 }, true, curve);
		r = Settle(s2, c, { far }, p);
		Expect(r.locked && Near(r.stretch, p.maxStretch, 0.001f), "10: 17 away -> (17 + 3) / 16, capped");
		std::vector<V3> j = Written(c, r);
		std::vector<V3> line{ c.root, far.point };
		line.insert(line.end(), curve.begin(), curve.end());
		float worst = 0;
		for (auto& x : j)
			worst = (std::max)(worst, PolyDist(x, line));
		Expect(worst < 0.01f, "10: stretched, the bent chain still lies on its line (the bend knows the stretch)");
		State s3;
		r = Settle(s3, c, { Opening(2, kVagina, { 0, 8, 0 }, { 0, 1, 0 }) }, p);
		Expect(r.locked && Near(r.stretch, 1.0f, 0.001f), "10: close -> never shortened");
	}
	{   // 11. a turned parent (the chain along -x): the corrections are in the parents' frames, the result the same
		// (the parent turns about z; the path bends in z, so the two turns do not commute)
		Chain c = Shaft({ -1, 0, 0 });
		std::vector<V3> path{ { -11, 0, 0 }, { -13, 0, 1.5f }, { -14.5f, 0, 4 }, { -15, 0, 7 } };
		Target t = Opening(2, kVagina, { -8, 0, 0.5f }, { -1, 0, 0 }, true, path);
		State s;
		Result r = Settle(s, c, { t }, p);
		std::vector<V3> j = Written(c, r);
		std::vector<V3> line{ c.root, t.point };
		line.insert(line.end(), path.begin(), path.end());
		float worst = 0;
		for (auto& x : j)
			worst = (std::max)(worst, PolyDist(x, line));
		Expect(r.locked && worst < 0.01f, "11: with a turned parent the chain still lies on the path");
	}
	{   // 12. released: the corrections fade back to the animation, not snapped, and end
		Chain c = Shaft();
		State s;
		Settle(s, c, { Opening(2, kVagina, { 0, 12, 3 }, { 0, 1, 0 }) }, p);
		Result r = Update(s, c, {}, {}, p, kStep);
		Expect(!r.locked && r.active && Angle(r.local[0]) > 0.01f, "12: just released: still fading");
		r = Settle(s, c, {}, p, {}, 600);
		float left = 0;
		for (auto& q : r.local)
			left = (std::max)(left, Angle(q));
		Expect(!r.active && left < 1e-4f && std::fabs(r.stretch - 1.0f) < 1e-4f, "12: faded out");
	}
	{   // 13. two openings: the smaller miss wins; the lock then stays when the other gets closer
		Chain c = Shaft();
		Target far = Opening(2, kVagina, { 0, 12, 4 }, { 0, 1, 0 });
		Target nearer = Opening(3, kMouth, { 0, 12, 1 }, { 0, 1, 0 });
		State s;
		Expect(Settle(s, c, { far, nearer }, p).targetOwner == 3, "13: the smaller miss is chosen");
		Target nowBetter = Opening(2, kVagina, { 0, 12, 0.2f }, { 0, 1, 0 });
		Expect(Update(s, c, { nowBetter, nearer }, {}, p, kStep).targetOwner == 3, "13: a held lock is not stolen");
		// the miss decides, not the angle: a close opening missed by 1.5 (14 degrees) over a far one
		// missed by 3 (11.5 degrees)
		Target close = Opening(4, kAnus, { 0, 6, 1.5f }, { 0, 1, 0 });
		Target farther = Opening(5, kVagina, { 0, 15, 3 }, { 0, 1, 0 });
		State s2;
		Expect(Settle(s2, c, { farther, close }, p).targetOwner == 4, "13: the smaller miss wins over the smaller angle");
	}
	{   // 14. smoothing: one frame moves only part of the way
		Chain c = Shaft();
		State s;
		Result r = Update(s, c, { Opening(2, kVagina, { 0, 12, 3 }, { 0, 1, 0 }) }, {}, p, kStep);
		Expect(r.newLock, "14: a new lock says so");
		Expect(Angle(r.local[0]) > 0.0f && Angle(r.local[0]) < r.angle * 0.5f, "14: the first frame turns a little");
	}
	{   // 15. a hand on the shaft: held, never aimed; and not for handHold after it lets go
		Chain c = Shaft();
		Target t = Opening(2, kVagina, { 0, 12, 1 }, { 0, 1, 0 });
		std::vector<Hand> hand{ Hand{ 2, { 0, 8, 2.5f } } };
		State s;
		Result r = Settle(s, c, { t }, p, hand);
		Expect(r.held && !r.locked, "15: a hand around the shaft: a hand job, not aimed at her");
		r = Update(s, c, { t }, {}, p, kStep);
		Expect(!r.locked, "15: the hand gone a moment: still not aimed");
		r = Settle(s, c, { t }, p, {}, (int)(p.handHold * 60) + 5);
		Expect(r.locked, "15: handHold later: aimed again");
		State s2;
		Settle(s2, c, { t }, p);
		r = Update(s2, c, { t }, hand, p, kStep);
		Expect(!r.locked, "15: a hand arriving releases a lock at once");
	}
	{   // 16. a hand at the shaft's root (steadying it) does not count
		Chain c = Shaft();
		State s;
		Result r = Settle(s, c, { Opening(2, kVagina, { 0, 12, 1 }, { 0, 1, 0 }) }, p, { Hand{ 2, { 0, 0.5f, 3.5f } } });
		Expect(!r.held && r.locked, "16: a hand at the root: still aimed");
	}
	{   // 18. a gripping hand is the target while it grips: the shaft runs through its grip, not into her
		Chain c = Shaft();
		Target her = Opening(2, kVagina, { 0, 12, 1 }, { 0, 1, 0 });
		Target grip = Opening(2, kHand, { 0.6f, 7, 1.2f }, { 0, -1, 0.1f });   // axis given the other way round
		std::vector<Hand> knuckle{ Hand{ 2, { 0, 7, 3 } } };
		State s;
		Result r = Settle(s, c, { her, grip }, p, knuckle);
		Expect(r.held && r.locked && r.targetKind == kHand, "18: gripped: locked on the hand, not on her");
		std::vector<V3> j = Written(c, r);
		Target o = grip;
		o.in = Scale(o.in, -1.0f);                                   // entered from the shaft's side
		std::vector<V3> line{ c.root, o.point, Add(o.point, Scale(o.in, 24.0f)) };
		float worst = 0;
		for (auto& x : j)
			worst = (std::max)(worst, PolyDist(x, line));
		Expect(worst < 0.01f, "18: the shaft runs through the grip, along it (either way round)");
		Expect(Near(r.stretch, 1.0f, 1e-4f), "18: a hand never stretches the shaft");
		Target tipGrip = Opening(2, kHand, { 0.5f, 15, 0.5f }, { 0, 1, 0 });   // near the tip: her own would stretch
		State s4;
		r = Settle(s4, c, { tipGrip }, p, { Hand{ 2, { 0, 14, 2.5f } } });
		Expect(r.locked && Near(r.stretch, 1.0f, 1e-4f), "18: a grip at the tip does not stretch it either");
		State s2;
		Expect(!Settle(s2, c, { grip }, p).locked, "18: a hand that does not hold the shaft is no target");
		State s3;
		Target own = grip;
		own.owner = 1;
		Expect(Settle(s3, c, { own }, p, knuckle).locked, "18: his own hand grips too");
	}
	{   // 19. no lag: a head bobbing 6 units a second (a pull-out) stays ON the shaft, frame by frame
		Chain c = Shaft();
		State s;
		V3 at{ 0, 12, 1 };
		Target t = Opening(2, kMouth, at, { 0, 1, 0 });
		Settle(s, c, { t }, p);
		float worst = 0;
		for (int f = 0; f < 60; f++) {
			at.z += 0.1f * std::sin(f * 0.3f);                      // up and down, 6 units/s at the fastest
			at.y -= 0.05f;                                         // and backing off
			t.point = at;
			Result r = Update(s, c, { t }, {}, p, kStep);
			std::vector<V3> j = Written(c, r);
			std::vector<V3> line{ c.root, t.point, Add(t.point, Scale(t.in, 24.0f)) };
			for (auto& x : j)
				worst = (std::max)(worst, PolyDist(x, line));
		}
		Expect(worst < 0.01f, "19: a moving mouth is tracked exactly every frame (no lag through her chin)");
	}
	{   // 20. a switch between openings fades from the pose on screen, not a jump; a release says why
		Chain c = Shaft();
		Target a = Opening(2, kMouth, { 0, 12, 2 }, { 0, 1, 0 });
		Target b = Opening(3, kMouth, { 0, 12, -2 }, { 0, 1, 0 });
		State s;
		Result r = Settle(s, c, { a }, p);
		V3 tipA = Written(c, r).back();
		r = Update(s, c, { b }, {}, p, kStep);                      // a is gone, b is there
		V3 tipNow = Written(c, r).back();
		Expect(r.locked && r.targetOwner == 3, "20: the lock moves to the other opening");
		Expect(Length(Sub(tipNow, tipA)) < 1.0f, "20: the first frame after the switch is near the old pose (no jump)");
		State s2;
		Settle(s2, c, { a }, p);
		Target far = a;
		far.point = { 9, 12, 0 };                                  // the animation now misses it by 9
		r = Update(s2, c, { far }, {}, p, kStep);
		Expect(r.released && std::string(r.why).find("too far") != std::string::npos, "20: a release says why");
	}
	{   // 17. FromTo / matrix round trip
		V3 a = Normalized({ 1, 2, 3 }), b = Normalized({ -2, 0.5f, 1 });
		Quat q = FromTo(a, b);
		Expect(Length(Sub(Rotate(q, a), b)) < 1e-4f, "17: FromTo turns a onto b");
		Quat back = FromMatrix(ToMatrix(q));
		Expect(Length(Sub(Rotate(back, a), b)) < 1e-4f, "17: matrix round trip keeps the turn");
		M3 m = ToMatrix(q);
		V3 viaM{ m.m[0][0] * a.x + m.m[0][1] * a.y + m.m[0][2] * a.z, m.m[1][0] * a.x + m.m[1][1] * a.y + m.m[1][2] * a.z,
			m.m[2][0] * a.x + m.m[2][1] * a.y + m.m[2][2] * a.z };
		Expect(Length(Sub(viaM, b)) < 1e-4f, "17: ToMatrix is v' = m v");
	}
	{   // 21. A grip's middle is where its curled fingers wrap, not the average of their joints
		// four fingers along x, each curled around the x axis at radius 2 (a shaft of 1.55 and the
		// finger's own flesh), knuckle on top and curling down over 160 degrees
		auto ring = [](float x, float r, float from, float to) {
			std::vector<V3> j;
			for (int k = 0; k < 3; k++) {
				float a = (from + (to - from) * k / 2.0f) / kDeg;
				j.push_back({ x, r * std::cos(a), r * std::sin(a) });
			}
			return j;
		};
		auto grip = [&](std::vector<std::vector<V3>> fingers, Quat turn, V3 shift, V3& out) {
			V3 js[4][3];
			for (int f = 0; f < 4; f++)
				for (int k = 0; k < 3; k++)
					js[f][k] = Add(Rotate(turn, fingers[f][k]), shift);
			return GripCentre(js, 4.0f, out);
		};
		std::vector<std::vector<V3>> curled;
		V3 mean{};
		for (int f = 0; f < 4; f++) {
			curled.push_back(ring(0.8f * f, 2.0f, 90.0f, 250.0f));
			for (auto& q : curled.back())
				mean = Add(mean, Scale(q, 1.0f / 12.0f));
		}
		V3 at;
		Expect(grip(curled, Quat{}, {}, at) && Length(Sub(at, { 1.2f, 0, 0 })) < 0.01f,
			"21: the grip is on the axis the fingers wrap around");
		Expect(Length(Sub(mean, { 1.2f, 0, 0 })) > 0.8f, "21: (the joints' average is 0.9 off: the case can fail)");
		Quat turn = FromTo(Normalized({ 1, 0, 0 }), Normalized({ 0.3f, -0.8f, 0.5f }));
		V3 shift{ 12, -7, 40 };
		Expect(grip(curled, turn, shift, at) && Length(Sub(at, Add(Rotate(turn, { 1.2f, 0, 0 }), shift))) < 0.01f,
			"21: a hand turned and moved: the grip goes with it");
		std::vector<std::vector<V3>> flat(4);
		for (int f = 0; f < 4; f++)
			flat[f] = { { 0.8f * f, 0, 0 }, { 0.8f * f, 2.5f, 0 }, { 0.8f * f, 4.5f, 0 } };
		Expect(!grip(flat, Quat{}, {}, at), "21: an open hand, fingers straight, is no grip");
		std::vector<std::vector<V3>> slack(4);
		for (int f = 0; f < 4; f++)
			slack[f] = ring(0.8f * f, 30.0f, 90.0f, 99.0f);   // bent a little: a circle 30 wide
		Expect(!grip(slack, Quat{}, {}, at), "21: fingers barely bent are no grip");
		std::vector<std::vector<V3>> one = flat;
		one[1] = curled[1];
		Expect(!grip(one, Quat{}, {}, at), "21: one curled finger is not a grip");
		std::vector<std::vector<V3>> two = flat;
		two[1] = curled[1];
		two[3] = curled[3];
		Expect(grip(two, Quat{}, {}, at) && Length(Sub(at, { 1.6f, 0, 0 })) < 0.01f,
			"21: two curled fingers are, and the straight ones do not pull it off the axis");
	}

	if (failures) {
		std::printf("%d expectation(s) failed\n", failures);
		return 1;
	}
	std::printf("aim: every expectation holds\n");
	return 0;
}
