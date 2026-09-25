// fo4-ocbpc: the penis as one tube for OCBPC's collisions (Tube.h), tested outside the game. Build and run:
// tests/tube/run.bat. The chain is the one the fork sees at run time: BodyTalk4's Penis_01..05 along the
// axis at 0.2, 3.28, 6.16, 9.17, 11.84 (fo4-anatomy glans_profile.py), the shaft x0.85 (collider 2.0 -> 1.7),
// the head x1.25 (1.8 -> 2.25), shaped by the glans profile (Glans.h) and less the tube's skin (0.2).
#include "Glans.h"
#include "Tube.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}
static bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) < tol; }

struct V
{
	float x, y, z;
	V operator+(const V& o) const { return { x + o.x, y + o.y, z + o.z }; }
	V operator-(const V& o) const { return { x - o.x, y - o.y, z - o.z }; }
	V operator*(float s) const { return { x * s, y * s, z * s }; }
};
struct P { V pos; float r; };
static float Len(const V& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// the old way, for comparison: every overlapping ball's push, ADDED (Collision.cpp IsItColliding)
static V SumOfBalls(const std::vector<P>& balls, const V& c, float rs)
{
	V push{ 0, 0, 0 };
	for (auto& b : balls) {
		V d = c - b.pos;
		float dist = Len(d), lim = b.r + rs;
		if (dist < lim && dist > 1e-5f)
			push = push + d * ((lim - dist) / dist);
	}
	return push;
}

int main()
{
	const float skin = 0.2f;
	const std::vector<Glans::Step> glans = { { -0.85f, 0.90f }, { -0.60f, 1.00f }, { -0.35f, 0.93f },
		{ -0.12f, 0.81f }, { 0.0f, 0.73f } };
	std::vector<P> balls = { { { 0, 0, 0.2f }, 1.7f }, { { 0, 0, 3.28f }, 1.7f }, { { 0, 0, 6.16f }, 1.7f },
		{ { 0, 0, 9.17f }, 1.7f }, { { 0, 0, 11.84f }, 2.25f } };
	std::vector<P> tube = balls;
	Glans::Shape(tube, glans, skin);
	for (auto& p : tube)
		p.r -= skin;                                // the flesh: the collider less the skin, as the mouth reads it

	{   // 1. along the shaft body, a labia sphere (r 1.2) is pushed to the same place wherever it is
		float lo = 1e9f, hi = -1e9f;
		for (float z = 1.0f; z <= 8.5f; z += 0.25f) {
			V push;
			V c{ 1.0f, 0.0f, z };
			Expect(Tube::Push(tube, c, 1.2f, push), "1: a lip inside the shaft is pushed");
			float out = Len(c + push - V{ 0, 0, z });
			lo = out < lo ? out : lo;
			hi = out > hi ? out : hi;
		}
		std::printf("1: along the shaft body a labia sphere ends %.3f .. %.3f from the axis (flesh 1.5 + 1.2)\n", lo, hi);
		Expect(Near(lo, 2.7f, 0.01f) && Near(hi, 2.7f, 0.01f), "1: it rests on the flesh (1.5) everywhere: no ride");
		float blo = 1e9f, bhi = -1e9f;
		for (float z = 1.0f; z <= 8.5f; z += 0.25f) {
			V c{ 1.0f, 0.0f, z };
			float out = Len(c + SumOfBalls(balls, c, 1.2f) - V{ 0, 0, z });
			blo = out < blo ? out : blo;
			bhi = out > bhi ? out : bhi;
		}
		std::printf("   the balls, added: %.3f .. %.3f\n", blo, bhi);
		Expect(bhi - blo > 0.3f, "1: (the balls DO ride: the comparison is meaningful)");
	}
	{   // 2. one push, not a sum: a lip between two joints is not pushed twice
		V push;
		V c{ 2.0f, 0.0f, 4.72f };                   // halfway between Penis_02 and Penis_03, inside
		Tube::Push(tube, c, 1.2f, push);
		Expect(Near(Len(c + push - V{ 0, 0, 4.72f }), 2.7f, 0.01f), "2: between two joints, one push to the surface");
	}
	{   // 3. the crown: where the mesh has it (0.6 R behind the tip bone), at its width
		const float R = 2.25f, crown = 11.84f - 0.6f * R;
		V push;
		V c{ 1.0f, 0.0f, crown };
		Tube::Push(tube, c, 0.6f, push);
		float out = Len(c + push - V{ 0, 0, crown });
		std::printf("3: at the crown (%.2f) an anus sphere (r 0.6) ends %.3f from the axis (crown %.2f + 0.6)\n",
			crown, out, R);
		Expect(Near(out, R + 0.6f, 0.01f), "3: the crown pushes to its full width");
	}
	{   // 4. the tip: no ball standing ahead of the glans; just past the tip, nothing
		V push;
		V ahead{ 0.0f, 0.0f, 11.84f + 0.73f * 2.25f + 0.6f + 0.05f };   // the tip's flesh is 0.73 R
		Expect(!Tube::Push(tube, ahead, 0.6f, push), "4: just past the tip's own flesh, no push");
		std::vector<P> old = balls;
		V ahead2{ 0.0f, 0.0f, 11.84f + 2.5f };     // the old ball reaches 2.25 + 0.6, the tube 1.64 + 0.6
		Expect(Len(SumOfBalls(old, ahead2, 0.6f)) > 0.3f && !Tube::Push(tube, ahead2, 0.6f, push),
			"4: 2.5 ahead of the tip bone the old ball still pushes, the tube does not");
	}
	{   // 5. out of reach, and dead on the line
		V push;
		Expect(!Tube::Push(tube, V{ 5.0f, 0.0f, 5.0f }, 1.2f, push), "5: a lip clear of the shaft is not pushed");
		Expect(Tube::Push(tube, V{ 0.0f, 0.0f, 5.0f }, 1.2f, push) && Near(Len(push), 2.7f, 0.01f),
			"5: on the axis itself: pushed out by the full depth, one way");
	}
	{   // 6. a bent chain (the aim's snake): the push is to the nearest segment, never through the bend
		std::vector<P> bent = { { { 0, 0, 0 }, 1.5f }, { { 0, 0, 3 }, 1.5f }, { { 3, 0, 6 }, 1.5f } };
		V push;
		V c{ -1.0f, 0.0f, 3.0f };                   // outside the bend's elbow
		Tube::Push(bent, c, 0.5f, push);
		V end = c + push;
		std::printf("6: at the elbow a sphere ends %.3f from the joint\n", Len(end - V{ 0, 0, 3 }));
		Expect(Near(Len(end - V{ 0, 0, 3 }), 2.0f, 0.01f), "6: at the elbow it rests on the joint's radius");
	}
	{   // 7. a toy (A-38): AddPropColliders's line, balls of 1.6 every 1.5 over 22.9 (the owner's first
		// zero-touch log): as one tube a lip rests on it wherever it is; as balls, two or three push at once
		std::vector<P> toy;
		for (float t = 0.0f; t <= 22.9f - 1.6f + 1e-3f; t += 1.5f)
			toy.push_back(P{ V{ 0, 0, t }, 1.6f });
		std::vector<P> toyTube = toy;
		for (auto& p : toyTube)
			p.r -= skin;
		float lo = 1e9f, hi = -1e9f, blo = 1e9f, bhi = -1e9f;
		for (float z = 2.0f; z <= 18.0f; z += 0.25f) {
			V c{ 1.0f, 0.0f, z }, push;
			Tube::Push(toyTube, c, 1.7f, push);
			float out = Len(c + push - V{ 0, 0, z });
			lo = out < lo ? out : lo;
			hi = out > hi ? out : hi;
			float b = Len(c + SumOfBalls(toy, c, 1.7f) - V{ 0, 0, z });
			blo = b < blo ? b : blo;
			bhi = b > bhi ? b : bhi;
		}
		std::printf("7: along a toy an inner-lip sphere (r 1.7) ends %.3f .. %.3f from its axis; as balls %.3f .. %.3f\n",
			lo, hi, blo, bhi);
		Expect(Near(lo, 1.4f + 1.7f, 0.01f) && Near(hi, 1.4f + 1.7f, 0.01f), "7: on the toy's tube: no ride");
		// the toy's balls are DENSE (1.5 apart, reach 3.3): they barely ride, but two or three push at once
		// and ADD, so a lip went out to ~5.0 where the toy's surface is 3.1 (measured by this test)
		Expect(blo > 1.4f * (1.6f + 1.7f), "7: (as balls a lip is shoved well past the toy: the comparison is meaningful)");
	}
	{   // 8. whom a tube pushes (Tube::Reaches): a penis never its owner; a toy only [Props] targets, its
		// holder's own included (solo scenes)
		Expect(Tube::Reaches(false, false, false), "8: a partner's penis pushes her");
		Expect(!Tube::Reaches(false, true, true), "8: never its own owner's body");
		Expect(Tube::Reaches(true, true, true), "8: a toy pushes its holder's own lips");
		Expect(!Tube::Reaches(true, false, false), "8: a toy never pushes a bone that is not a [Props] target");
		Expect(Tube::Reaches(true, false, true), "8: a toy pushes a partner's target bone");
	}
	if (failures) {
		std::printf("%d expectation(s) failed\n", failures);
		return 1;
	}
	std::printf("tube: every expectation holds\n");
	return 0;
}
