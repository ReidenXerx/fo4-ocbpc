// fo4-ocbpc: the penis finds its opening (A-28), tested outside the game. Build and run: tests/aim/run.bat.
// Each case states what it proves; the process exits 1 on the first failed expectation's summary.
#include "AimSolve.h"

#include <cmath>
#include <cstdio>
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
static float Deg(float r) { return r * 57.29578f; }

// A shaft along +y from the origin, 16 long (ZeX's chain is 16.1), both in a scene.
static Chain Shaft(Quat parent = Quat{})
{
	Chain c;
	c.owner = 1;
	c.inScene = true;
	c.base = { 0, 0, 0 };
	c.tip = { 0, 16, 0 };
	c.length = 16.0f;
	c.parent = parent;
	return c;
}

static Target Opening(std::uint32_t owner, int kind, V3 point, V3 in, bool inScene = true)
{
	Target t;
	t.owner = owner;
	t.kind = kind;
	t.point = point;
	t.in = Normalized(in);
	t.inScene = inScene;
	return t;
}

// Where the aimed shaft points, in the world: the correction is in the parent's frame (P l P^-1).
static V3 AimedDirection(const Chain& c, const Result& r)
{
	Quat world = Mul(Mul(c.parent, r.local), Conj(c.parent));
	return Normalized(Rotate(world, Sub(c.tip, c.base)));
}

static Result Settle(State& s, const Chain& c, const std::vector<Target>& ts, const Params& p, int frames = 240)
{
	Result r;
	for (int i = 0; i < frames; i++)
		r = Update(s, c, ts, p, 1.0f / 60.0f);
	return r;
}

// The distance from a point to the line through base along dir.
static float OffLine(V3 base, V3 dir, V3 point)
{
	V3 q = Sub(point, base);
	return Length(Sub(q, Scale(dir, Dot(q, dir))));
}

int main()
{
	Params p;

	{   // 1. a vagina 10 degrees off: locked, and the settled shaft runs through the aim point
		Chain c = Shaft();
		V3 point{ 0, 12, 12 * std::tan(10.0f / 57.29578f) };
		Target t = Opening(2, kVagina, point, { 0, 1, 0 });
		State s;
		Result r = Settle(s, c, { t }, p);
		Expect(r.locked && r.targetOwner == 2 && r.targetKind == kVagina, "1: locks on a vagina 10 degrees off");
		V3 aim = Add(t.point, Scale(t.in, p.depth));
		Expect(OffLine(c.base, AimedDirection(c, r), aim) < 0.05f, "1: the settled shaft passes through the aim point");
		Expect(Near(Deg(r.angle), 9.1f, 1.5f), "1: the turn asked for is about the offset (aim point is 2 deeper)");
	}
	{   // 2. an opening 90 degrees off is left to the animation
		Chain c = Shaft();
		State s;
		Result r = Settle(s, c, { Opening(2, kVagina, { 12, 0, 0 }, { 1, 0, 0 }) }, p);
		Expect(!r.locked && !r.active, "2: 90 degrees off is never captured");
	}
	{   // 3. one's own opening is never a target
		Chain c = Shaft();
		State s;
		Result r = Settle(s, c, { Opening(1, kMouth, { 0, 12, 0 }, { 0, 1, 0 }) }, p);
		Expect(!r.locked, "3: never one's own mouth");
	}
	{   // 4. out of a scene: nothing, unless the scene is not required
		Chain c = Shaft();
		Target t = Opening(2, kVagina, { 0, 12, 1 }, { 0, 1, 0 }, false);
		State s;
		Expect(!Settle(s, c, { t }, p).locked, "4: an opening out of a scene is left alone");
		Params q = p;
		q.requireScene = false;
		State s2;
		Expect(Settle(s2, c, { t }, q).locked, "4: ...and taken when requireScene is off");
	}
	{   // 5. hysteresis: a lock is kept up to keepAngle, released past it; a new one needs captureAngle
		Chain c = Shaft();
		auto at = [](float deg) { return V3{ 12 * std::sin(deg / 57.29578f), 12 * std::cos(deg / 57.29578f), 0 }; };
		Params q = p;
		q.depth = 0.0f;                              // aim at the centre itself, so the angles are exact
		State s;
		Settle(s, c, { Opening(2, kVagina, at(30), at(30)) }, q);
		Expect(s.locked, "5: locks at 30 degrees");
		Result r = Update(s, c, { Opening(2, kVagina, at(40), at(40)) }, q, 1.0f / 60.0f);
		Expect(r.locked, "5: kept at 40 (past capture, within keep)");
		r = Update(s, c, { Opening(2, kVagina, at(50), at(50)) }, q, 1.0f / 60.0f);
		Expect(!r.locked, "5: released at 50 (past keep)");
		State fresh;
		r = Update(fresh, c, { Opening(2, kVagina, at(40), at(40)) }, q, 1.0f / 60.0f);
		Expect(!r.locked, "5: a NEW lock at 40 is refused (past capture)");
	}
	{   // 6. entered from inside, or from the side: refused
		Chain c = Shaft();
		State s;
		Expect(!Settle(s, c, { Opening(2, kAnus, { 0, 12, 0 }, { 0, -1, 0 }) }, p).locked,
			"6: an opening whose inside faces the shaft is not entered from inside");
		State s2;
		Expect(!Settle(s2, c, { Opening(2, kAnus, { 0, 12, 0 }, { 1, 0, 0 }) }, p).locked,
			"6: nor from the side (90 degrees to its axis)");
	}
	{   // 7. an opening behind the root, or too close / too far
		Chain c = Shaft();
		State s;
		Expect(!Settle(s, c, { Opening(2, kVagina, { 0, -8, 0 }, { 0, -1, 0 }) }, p).locked, "7: behind the root: no");
		State s2;
		Expect(!Settle(s2, c, { Opening(2, kVagina, { 0, 1, 0 }, { 0, 1, 0 }) }, p).locked, "7: closer than minReach: no");
		State s3;
		Expect(!Settle(s3, c, { Opening(2, kVagina, { 0, 22, 0 }, { 0, 1, 0 }) }, p).locked, "7: past reach (1.3 x 16): no");
	}
	{   // 8. the stretch: enough for minInside past the entrance, capped at maxStretch
		Chain c = Shaft();
		State s;
		Result r = Settle(s, c, { Opening(2, kVagina, { 0, 14, 0 }, { 0, 1, 0 }) }, p);
		Expect(r.locked && Near(r.stretch, (14.0f + p.minInside) / 16.0f, 0.01f), "8: 14 away -> (14 + 3) / 16");
		State s2;
		r = Settle(s2, c, { Opening(2, kVagina, { 0, 20, 0 }, { 0, 1, 0 }) }, p);
		Expect(r.locked && Near(r.stretch, p.maxStretch, 0.001f), "8: 20 away -> capped at maxStretch");
		State s3;
		r = Settle(s3, c, { Opening(2, kVagina, { 0, 8, 0 }, { 0, 1, 0 }) }, p);
		Expect(r.locked && Near(r.stretch, 1.0f, 0.001f), "8: close -> never shortened");
	}
	{   // 9. the parent turned 90 degrees about z: the correction is in its frame, the world aim the same
		float h = std::sqrt(0.5f);
		Quat turned{ h, 0, 0, h };
		Chain c = Shaft(turned);
		V3 point{ 0, 12, 2 };
		Target t = Opening(2, kVagina, point, { 0, 1, 0 });
		State s;
		Result r = Settle(s, c, { t }, p);
		Expect(OffLine(c.base, AimedDirection(c, r), Add(point, Scale(t.in, p.depth))) < 0.05f,
			"9: with a turned parent the shaft still runs through the aim point");
	}
	{   // 10. released: the correction fades back to the animation, and is exactly nothing at the end
		Chain c = Shaft();
		State s;
		Settle(s, c, { Opening(2, kVagina, { 0, 12, 3 }, { 0, 1, 0 }) }, p);
		Result r = Update(s, c, {}, p, 1.0f / 60.0f);
		Expect(!r.locked && r.active && Angle(r.local) > 0.01f, "10: just released: still fading, not snapped");
		r = Settle(s, c, {}, p, 600);
		Expect(!r.active && Angle(r.local) < 1e-4f && std::fabs(r.stretch - 1.0f) < 1e-4f,
			"10: faded out: nothing left to write (Aim.cpp then puts the animation's pose back exactly)");
	}
	{   // 11. two openings: the smaller turn wins; the lock then stays even when the other gets closer
		Chain c = Shaft();
		Target far = Opening(2, kVagina, { 0, 12, 4 }, { 0, 1, 0 });
		Target nearer = Opening(3, kMouth, { 0, 12, 1 }, { 0, 1, 0 });
		State s;
		Result r = Settle(s, c, { far, nearer }, p);
		Expect(r.targetOwner == 3, "11: the smaller turn is chosen");
		Target nowBetter = Opening(2, kVagina, { 0, 12, 0.2f }, { 0, 1, 0 });
		r = Update(s, c, { nowBetter, nearer }, p, 1.0f / 60.0f);
		Expect(r.targetOwner == 3, "11: a held lock is not stolen by a better one");
	}
	{   // 12. smoothing: one frame moves only part of the way, and never past it
		Chain c = Shaft();
		State s;
		Result r = Update(s, c, { Opening(2, kVagina, { 0, 12, 3 }, { 0, 1, 0 }) }, p, 1.0f / 60.0f);
		float full = r.angle;
		Expect(r.newLock, "12: a new lock says so");
		Expect(Angle(r.local) > 0.0f && Angle(r.local) < full * 0.5f, "12: the first frame turns a little, not all");
	}
	{   // 13. FromTo / matrix round trip
		V3 a = Normalized({ 1, 2, 3 }), b = Normalized({ -2, 0.5f, 1 });
		Quat q = FromTo(a, b);
		Expect(Length(Sub(Rotate(q, a), b)) < 1e-4f, "13: FromTo turns a onto b");
		Quat back = FromMatrix(ToMatrix(q));
		Expect(Length(Sub(Rotate(back, a), b)) < 1e-4f, "13: matrix round trip keeps the turn");
		M3 m = ToMatrix(q);
		V3 viaM{ m.m[0][0] * a.x + m.m[0][1] * a.y + m.m[0][2] * a.z, m.m[1][0] * a.x + m.m[1][1] * a.y + m.m[1][2] * a.z,
			m.m[2][0] * a.x + m.m[2][1] * a.y + m.m[2][2] * a.z };
		Expect(Length(Sub(viaM, b)) < 1e-4f, "13: ToMatrix is v' = m v");
	}

	if (failures) {
		std::printf("%d expectation(s) failed\n", failures);
		return 1;
	}
	std::printf("aim: every expectation holds\n");
	return 0;
}
