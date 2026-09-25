// fo4-ocbpc: the lips around what is in her mouth (A-32), tested outside the game. Build and run: tests/lips/run.bat.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
// The table is the female head's, as fo4-anatomy tools/lips.py measured it on the game's heads (2026-09-25).
#include "LipFit.h"
#include "Glans.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace LipFit;

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

static const float kXs[kSamples] = { -1.2f, -0.8f, -0.4f, 0.0f, 0.4f, 0.8f, 1.2f };

static Table Female()
{
	struct Row { int id; float u[kSamples]; float l[kSamples]; float dl, dr; };
	static const Row rows[] = {
		{ 2, { -0.523f, -0.164f, -0.066f, -0.066f, -0.075f, -0.156f, -0.367f }, { -1.457f, -1.734f, -1.888f, -1.910f, -1.895f, -1.746f, -1.509f }, -0.27f, 0.22f },
		{ 22, { 0.003f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.003f }, { -0.022f, -0.172f, -0.316f, -0.317f, -0.316f, -0.172f, -0.021f }, 0.08f, -0.08f },
		{ 46, { 0.005f, 0.145f, 0.179f, 0.171f, 0.177f, 0.154f, 0.033f }, { 0.0f, 0.002f, 0.0f, 0.0f, 0.0f, 0.001f, 0.0f }, 0.08f, -0.07f },
		{ 21, { 0.37f, 0.38f, 0.34f, 0.23f, 0.12f, 0.04f, 0.0f }, { 0.01f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 0.04f, 0.0f },
		{ 44, { 0.0f, 0.04f, 0.12f, 0.23f, 0.34f, 0.38f, 0.38f }, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f }, 0.0f, -0.04f },
		{ 11, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { -0.32f, -0.39f, -0.33f, -0.22f, -0.13f, -0.05f, -0.01f }, 0.0f, 0.0f },
		{ 34, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { -0.01f, -0.05f, -0.13f, -0.22f, -0.33f, -0.39f, -0.33f }, 0.0f, 0.0f },
		{ 20, { -0.24f, -0.28f, -0.26f, -0.18f, -0.10f, -0.03f, 0.0f }, { 0.02f, -0.05f, -0.07f, -0.07f, -0.07f, -0.05f, 0.01f }, 0.0f, 0.01f },
		{ 43, { 0.0f, -0.03f, -0.10f, -0.18f, -0.26f, -0.28f, -0.25f }, { 0.02f, -0.05f, -0.07f, -0.07f, -0.07f, -0.05f, 0.01f }, -0.01f, 0.0f },
		{ 12, { 0.03f, 0.03f, 0.04f, 0.05f, 0.04f, 0.03f, 0.03f }, { 0.29f, 0.30f, 0.25f, 0.17f, 0.10f, 0.04f, 0.0f }, 0.0f, 0.0f },
		{ 35, { 0.03f, 0.03f, 0.04f, 0.05f, 0.04f, 0.03f, 0.03f }, { 0.0f, 0.04f, 0.10f, 0.17f, 0.25f, 0.30f, 0.29f }, 0.0f, 0.0f },
		// Lip Corner Out: barely a lip's height; its own corner out 0.40 / 0.34, the other 0.11 / 0.12
		{ 8, { 0.06f, 0.01f, -0.02f, -0.02f, 0.0f, -0.02f, 0.04f }, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, -0.40f, 0.11f },
		{ 31, { -0.01f, -0.02f, 0.0f, -0.01f, -0.02f, 0.01f, 0.06f }, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, -0.12f, 0.34f },
	};
	Table t;
	t.restLeft = -1.60f;                            // the rim's ends at rest
	t.restRight = 1.59f;
	for (const Row& r : rows) {
		int m = t.count++;
		t.id[m] = r.id;
		t.left[m] = r.dl;
		t.right[m] = r.dr;
		for (int k = 0; k < kSamples; k++) {
			t.up[m][k] = r.u[k];
			t.lo[m][k] = r.l[k];
		}
	}
	return t;
}

// a round thing crossing the lip plane: centre (x, height) and radii across and up
static Want Round(float cx, float cu, float rx, float ru)
{
	Want w;
	w.across = true;
	w.lo = cx - rx;
	w.hi = cx + rx;
	for (int k = 0; k < kSamples; k++) {
		float d = (kXs[k] - cx) / rx;
		if (std::fabs(d) < 1.0f) {
			float h = ru * std::sqrt(1.0f - d * d);
			w.spans[k] = true;
			w.top[k] = cu + h;
			w.bottom[k] = cu - h;
		}
	}
	return w;
}

// the worst an edge is inside the surface, and the worst gap, over the spanned samples
static void Judge(const Table& t, const Want& want, const float* w, float& worstIn, float& worstGap)
{
	float u[kSamples], l[kSamples];
	Edges(t, w, u, l);
	worstIn = -1e9f;                   // below 0: every edge clears the surface
	worstGap = 0.0f;
	for (int k = 0; k < kSamples; k++) {
		if (!want.spans[k])
			continue;
		worstIn = (std::max)(worstIn, (std::max)(want.top[k] - u[k], l[k] - want.bottom[k]));
		worstGap = (std::max)(worstGap, (std::max)(u[k] - want.top[k], want.bottom[k] - l[k]));
	}
}

// The same head with where its lips meet (tools/lips.py, 2026-09-26): each morph's move of the two corner
// vertices, and Lip Corner In (7 / 30), which only the corners show. Female() stays as it was measured for
// A-32 so its cases keep testing the fit without the hug.
static Table FemaleWithCorners()
{
	Table t = Female();
	struct Row { int id; float u[kSamples]; float l[kSamples]; float dl, dr; };
	static const Row in[] = {
		// the hug's knob only (tools/lips.py): its extremes' bulge and its <= 0.09 of lip move are dropped
		{ 7, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
		{ 30, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f },
	};
	for (const Row& r : in) {
		int m = t.count++;
		t.id[m] = r.id;
		t.left[m] = r.dl;
		t.right[m] = r.dr;
		for (int k = 0; k < kSamples; k++) {
			t.up[m][k] = r.u[k];
			t.lo[m][k] = r.l[k];
		}
	}
	struct Corner { int id; float l, r; };
	static const Corner corners[] = { { 2, 0.263f, -0.177f }, { 22, 0.084f, -0.084f }, { 46, 0.083f, -0.069f },
		{ 21, 0.04f, 0.0f }, { 44, 0.0f, -0.04f }, { 8, -0.40f, -0.063f }, { 31, 0.004f, 0.337f },
		{ 7, 0.323f, -0.057f }, { 30, -0.007f, -0.380f } };
	for (const Corner& c : corners)
		for (int m = 0; m < t.count; m++)
			if (t.id[m] == c.id) {
				t.cornerL[m] = c.l;
				t.cornerR[m] = c.r;
			}
	t.restCornerL = -1.60f;
	t.restCornerR = 1.585f;
	return t;
}

static float Weight(const Table& t, const float* w, int id)
{
	for (int m = 0; m < t.count; m++)
		if (t.id[m] == id)
			return w[m];
	return 0.0f;
}

int main()
{
	const Table t = Female();
	Params p;
	float w[kMaxMorphs], in, gap;

	// What a mouth can do (the table): the upper lip rises at most ~0.6 (Upper Lip Up, its funnel) and the
	// lower drops ~2.6 (Jaw Open, Lower Lip Down, its funnel). So a shaft wraps when it rides below the lip
	// line, as the aim puts it (mouthDrop 1.3), not centred on it (case 7).
	{   // 1. the shaft where the aim puts it (radius 1.3 visible, centre 1.0 below the lip line): lips on it
		Want want = Round(0.0f, -1.0f, 1.3f, 1.3f);
		Fit(t, want, p, nullptr, w);
		Judge(t, want, w, in, gap);
		std::printf("1: inside %.3f, gap %.3f, jaw %.2f\n", in, gap, Weight(t, w, 2));
		Expect(in < 0.06f, "1: no lip edge more than 0.06 inside the shaft");
		Expect(in < -0.02f, "1: and they rest a little OFF it (the clearance), not on the surface");
		Expect(gap < 0.45f, "1: no lip edge more than 0.45 off it (they rest on it)");
		Expect(Weight(t, w, 2) > 0.8f, "1: Jaw Open carries the bulk (a 2.6 tall shaft)");
	}
	{   // 2. a thin thing riding low (radius 0.5, centre 1.2 below): the upper lip comes DOWN onto it
		Want want = Round(0.0f, -1.2f, 0.5f, 0.5f);
		Fit(t, want, p, nullptr, w);
		Judge(t, want, w, in, gap);
		std::printf("2: inside %.3f, gap %.3f, upper lip down %.2f/%.2f, jaw %.2f\n", in, gap, Weight(t, w, 20),
			Weight(t, w, 43), Weight(t, w, 2));
		Expect(in < 0.06f, "2: never inside");
		Expect(Weight(t, w, 20) + Weight(t, w, 43) > 0.3f, "2: Upper Lip Down closes the upper lip onto it");
		Expect(Weight(t, w, 21) + Weight(t, w, 44) < 0.1f, "2: and Upper Lip Up stays off");
	}
	{   // 3. a finger (radius 0.5) at the lips: a small opening in the middle, the sides nearly closed
		Want want = Round(0.0f, -0.4f, 0.5f, 0.5f);
		Fit(t, want, p, nullptr, w);
		Judge(t, want, w, in, gap);
		float u[kSamples], l[kSamples];
		Edges(t, w, u, l);
		std::printf("3: inside %.3f, gap %.3f, jaw %.2f, sides open %.2f / %.2f\n", in, gap, Weight(t, w, 2), u[0] - l[0], u[6] - l[6]);
		Expect(in < 0.06f, "3: never inside");
		Expect(Weight(t, w, 2) < 0.6f, "3: the jaw opens far less than for the shaft");
		Expect(u[0] - l[0] < 0.45f && u[6] - l[6] < 0.45f, "3: the corners stay nearly closed (under 0.45)");
	}
	{   // 4. off to one side (centre x 0.6, 0.6 below): never inside
		Want want = Round(0.6f, -0.6f, 0.7f, 0.7f);
		Fit(t, want, p, nullptr, w);
		Judge(t, want, w, in, gap);
		std::printf("4: inside %.3f, gap %.3f\n", in, gap);
		Expect(in < 0.06f, "4: never inside, off centre");
	}
	{   // 5. nothing crosses: every weight off
		Want want;
		float start[kMaxMorphs];
		for (int m = 0; m < kMaxMorphs; m++)
			start[m] = 0.7f;
		Fit(t, want, p, start, w);
		float most = 0.0f;
		for (int m = 0; m < t.count; m++)
			most = (std::max)(most, w[m]);
		Expect(most < 0.05f, "5: nothing in the mouth: the lips close from any start");
	}
	{   // 6. the fit does not depend on where it starts (last frame's weights)
		Want want = Round(0.2f, -0.4f, 1.2f, 1.2f);
		float a[kMaxMorphs], b[kMaxMorphs], start[kMaxMorphs];
		for (int m = 0; m < kMaxMorphs; m++)
			start[m] = 1.0f;
		Fit(t, want, p, nullptr, a);
		Fit(t, want, p, start, b);
		float ea[kSamples], la[kSamples], eb[kSamples], lb[kSamples], worst = 0.0f;
		Edges(t, a, ea, la);
		Edges(t, b, eb, lb);
		for (int k = 0; k < kSamples; k++)
			worst = (std::max)(worst, (std::max)(std::fabs(ea[k] - eb[k]), std::fabs(la[k] - lb[k])));
		std::printf("6: edges from two starts differ by %.3f\n", worst);
		Expect(worst < 0.08f, "6: the same lips from either start");
	}
	{   // 7. too big for the mouth (radius 2.5): as open as it goes, every weight in [0, 1]
		Want want = Round(0.0f, 0.0f, 2.5f, 2.5f);
		Fit(t, want, p, nullptr, w);
		bool bounded = true;
		for (int m = 0; m < t.count; m++)
			bounded = bounded && w[m] >= 0.0f && w[m] <= 1.0f;
		Expect(bounded && Weight(t, w, 2) > 0.99f, "7: too big: Jaw Open full, nothing past 1");
	}
	{   // 8. centred ON the lip line (no mouth can wrap it: the upper lip would have to rise 1.35): the best
		// there is. The lower lip is not inside, and the upper lip is up as far as it goes.
		Want want = Round(0.0f, 0.0f, 1.3f, 1.3f);
		Fit(t, want, p, nullptr, w);
		float u[kSamples], l[kSamples];
		Edges(t, w, u, l);
		std::printf("8: upper lip up %.2f/%.2f, lower edge %.2f against %.2f\n", Weight(t, w, 21), Weight(t, w, 44), l[3],
			want.bottom[3]);
		Expect(Weight(t, w, 21) > 0.9f && Weight(t, w, 44) > 0.9f, "8: too high: the upper lip is up as far as it goes");
		Expect(l[3] <= want.bottom[3] + 0.06f, "8: and the lower lip is still under it");
	}

	{   // 9. the start is last frame's lips, clamped to [0, 1]: with no sweeps, that is what comes back
		Params still = p;
		still.steps = 0;
		float start[kMaxMorphs] = {};
		start[0] = 0.3f;
		start[1] = 1.7f;
		start[2] = -0.2f;
		Fit(t, Round(0.0f, -1.0f, 1.3f, 1.3f), still, start, w);
		Expect(w[0] == 0.3f && w[1] == 1.0f && w[2] == 0.0f, "9: the start is used, clamped");
	}
	{   // 10. ACROSS: a head wider than her mouth's rim (half-width 2.15, centre 1.0 below): the corners open
		// round it (Jaw Open widens, Corner Out opens; reach: -2.39 .. 2.26)
		Want want = Round(0.0f, -1.0f, 2.15f, 1.3f);
		Fit(t, want, p, nullptr, w);
		float el, er;
		Ends(t, w, el, er);
		std::printf("10: corners %.2f .. %.2f for a head %.2f .. %.2f; corner out %.2f/%.2f, funnels %.2f/%.2f\n", el, er,
			want.lo, want.hi, Weight(t, w, 8), Weight(t, w, 31), Weight(t, w, 22), Weight(t, w, 46));
		Expect(el <= want.lo + 0.02f && er >= want.hi - 0.02f, "10: both corners clear the head");
		Expect(Weight(t, w, 8) > 0.3f && Weight(t, w, 31) > 0.3f, "10: Corner Out opens both corners");
		// (a head this big cannot also be wrapped top to bottom near the corners: at x 1.2 the lower lip drops at
		// most ~1.84 and the head's underside is lower; while the head passes the lips the fit only minimises it)
	}
	{   // 11. a shaft narrower than the rim (half-width 1.3): the corners stay where they are
		Want want = Round(0.0f, -1.0f, 1.3f, 1.3f);
		Fit(t, want, p, nullptr, w);
		Expect(Weight(t, w, 8) < 0.05f && Weight(t, w, 31) < 0.05f, "11: a narrower shaft leaves the corners be");
	}
	{   // 12. off to the right (centre x 0.6, half-width 1.4: out to 2.0; the reach is ~2.2 with the funnels the
		// vertical fit uses): the right corner clears it and does at least the left's work (Left Corner Out
		// also pushes the right end 0.11, so the fit may use both)
		Want want = Round(0.6f, -1.0f, 1.4f, 1.3f);
		Fit(t, want, p, nullptr, w);
		float el, er;
		Ends(t, w, el, er);
		std::printf("12: corners %.2f .. %.2f for %.2f .. %.2f; corner out %.2f/%.2f, jaw %.2f\n", el, er, want.lo, want.hi,
			Weight(t, w, 8), Weight(t, w, 31), Weight(t, w, 2));
		Expect(er >= want.hi - 0.02f && Weight(t, w, 31) >= Weight(t, w, 8), "12: off to one side, that corner opens");
	}

	{   // 13. EXACTLY round it, the across terms alone: two corner morphs as the real ones (each opens its own
		// end and nudges the other), no lip heights. A corner that must move lands at the extent plus the
		// clearance: no further, no nearer. (On the real table the fit may take a corner past it when that
		// also lifts the upper lip where a tall head needs it: the vertical fit's right, not a miss.)
		Table c;
		c.restLeft = -1.60f;
		c.restRight = 1.59f;
		c.count = 2;
		c.id[0] = 8;  c.left[0] = -0.40f; c.right[0] = 0.11f;
		c.id[1] = 31; c.left[1] = -0.12f; c.right[1] = 0.34f;
		Want want;
		want.across = true;
		want.lo = -1.75f;
		want.hi = 1.70f;
		Fit(c, want, p, nullptr, w);
		float el, er;
		Ends(c, w, el, er);
		std::printf("13: corners %.3f .. %.3f, wanted %.3f .. %.3f\n", el, er, want.lo - p.clearance, want.hi + p.clearance);
		Expect(std::fabs(el - (want.lo - p.clearance)) < 0.02f && std::fabs(er - (want.hi + p.clearance)) < 0.02f,
			"13: each corner lands at the extent plus the clearance");
		want.lo = -1.95f;                            // the left needs most of its corner (0.8 of it)
		Fit(c, want, p, nullptr, w);
		Ends(c, w, el, er);
		Expect(std::fabs(el - (want.lo - p.clearance)) < 0.02f, "13: a corner far out still lands on its target");
		want.lo = -1.2f;
		want.hi = 1.2f;
		Fit(c, want, p, nullptr, w);
		Expect(w[0] < 0.01f && w[1] < 0.01f, "13: inside the rim, the corners stay put");
	}
	{   // 14. a shaft inside the rim: the across terms change nothing (no corner is pulled IN toward it)
		Want want = Round(0.0f, -1.0f, 1.3f, 1.3f);
		float a[kMaxMorphs], b[kMaxMorphs];
		Fit(t, want, p, nullptr, a);
		want.across = false;
		Fit(t, want, p, nullptr, b);
		float worst = 0.0f;
		for (int m = 0; m < t.count; m++)
			worst = (std::max)(worst, std::fabs(a[m] - b[m]));
		Expect(worst < 1e-4f, "14: inside the rim, the fit is the same with or without the corners");
	}
	{   // 16. the hug (the owner, 2026-09-26: "the corner of mouth still kinda static"): the corners close in on
		// a shaft narrower than the mouth, follow it out where it widens, and never into it
		const Table c = FemaleWithCorners();
		float cl, cr, el, er;
		Want thin = Round(0.0f, -1.0f, 1.1f, 1.1f);     // a thin shaft, where the aim puts it
		Params off = p;
		off.hug = 0.0f;
		Fit(c, thin, off, nullptr, w);
		Corners(c, w, cl, cr);
		float staticL = cl, staticR = cr;
		Fit(c, thin, p, nullptr, w);
		Corners(c, w, cl, cr);
		std::printf("16: a thin shaft (-1.10 .. 1.10): corners %.3f .. %.3f with the hug, %.3f .. %.3f without; corner in "
			"%.2f/%.2f\n", cl, cr, staticL, staticR, Weight(c, w, 7), Weight(c, w, 30));
		const float tL = thin.lo - p.clearance, tR = thin.hi + p.clearance;
		Expect(std::fabs(cl - tL) < std::fabs(staticL - tL) - 0.03f && std::fabs(cr - tR) < std::fabs(staticR - tR) - 0.03f,
			"16: the corners come nearer the thin shaft's sides than without the hug");
		Expect(std::fabs(cl - tL) < 0.1f && std::fabs(cr - tR) < 0.1f, "16: to within 0.1 of them");
		Expect(Weight(c, w, 7) + Weight(c, w, 30) > 0.1f, "16: with Lip Corner In, the knob that takes them in");
		Judge(c, thin, w, in, gap);
		Expect(in < 0.06f, "16: and the lips still clear it top and bottom");
		Want head = Round(0.0f, -1.0f, 2.1f, 1.6f);     // the head passing
		Fit(c, head, off, nullptr, w);
		float headL0, headR0;
		Corners(c, w, headL0, headR0);
		float in0, gap0;
		Judge(c, head, w, in0, gap0);
		Fit(c, head, p, nullptr, w);
		Corners(c, w, cl, cr);
		Ends(c, w, el, er);
		std::printf("16: the head (-2.10 .. 2.10): corners %.3f .. %.3f with the hug, %.3f .. %.3f without, rim ends "
			"%.3f .. %.3f; corner in %.2f/%.2f\n", cl, cr, headL0, headR0, el, er, Weight(c, w, 7), Weight(c, w, 30));
		Expect(Weight(c, w, 8) > 0.95f && Weight(c, w, 31) > 0.95f && cl <= headL0 + 1e-3f && cr >= headR0 - 1e-3f,
			"16: the head takes the corners as far out as Corner Out goes");
		Judge(c, head, w, in, gap);
		std::printf("16: the head, lips: inside %.3f with the hug, %.3f without\n", in, in0);
		// A head this size is more than the lips can clear (0.48 inside without the hug): the fit takes any
		// morph that helps them, Corner In's small lower-lip drop too. The hug may cost them a little there.
		Expect(in < in0 + 0.06f, "16: the hug costs the lips at most 0.06 of clearance (it does not fight the jaw)");
		Want side = Round(0.6f, -1.0f, 1.1f, 1.1f);     // off to her right
		Fit(c, side, off, nullptr, w);
		float sideL0, sideR0;
		Corners(c, w, sideL0, sideR0);
		Fit(c, side, p, nullptr, w);
		Corners(c, w, cl, cr);
		std::printf("16: off to one side (-0.50 .. 1.70): corners %.3f .. %.3f with the hug, %.3f .. %.3f without\n",
			cl, cr, sideL0, sideR0);
		Expect(cl > sideL0 + 0.05f, "16: off to one side, the far corner closes in");
		Expect(cr > sideR0 + 0.02f, "16: and the near one goes out toward it");
	}
	{   // 17. the owner's photo from inside the shaft (Photo223-224): the shaft taller than the mouth can open,
		// the jaw at full, which draws the corners in ~0.2 - and they stayed in the shaft. They must open to it.
		const Table c = FemaleWithCorners();
		Want tall = Round(0.0f, -1.3f, 1.53f, 1.53f);    // the shaft (1.5 flesh) where the aim puts it
		Fit(c, tall, p, nullptr, w);
		float cl, cr;
		Corners(c, w, cl, cr);
		std::printf("17: a shaft taller than the mouth (-1.53 .. 1.53): corners %.3f .. %.3f, jaw %.2f, corner out "
			"%.2f/%.2f in %.2f/%.2f\n", cl, cr, Weight(c, w, 2), Weight(c, w, 8), Weight(c, w, 31), Weight(c, w, 7),
			Weight(c, w, 30));
		// with the jaw and the funnels the lips need, Corner Out at full reaches -1.53 / 1.49 on this head: the
		// corners end within 0.1 of the shaft's sides (the photo's left corner was ~0.27 in, Corner In at 1.0)
		Expect(cl <= tall.lo - p.clearance + 0.1f && cr >= tall.hi + p.clearance - 0.1f,
			"17: where the lips meet, within 0.1 of the shaft's sides");
		Expect(Weight(c, w, 8) > 0.95f && Weight(c, w, 31) > 0.95f, "17: Corner Out all the way to get there");
		Expect(Weight(c, w, 7) < 0.02f && Weight(c, w, 30) < 0.02f, "17: no Corner In while a corner is short of it");
		Expect(Weight(c, w, 2) > 0.95f, "17: and the jaw still all the way open for the lips");
	}
	{   // 15. the glans (Glans.h): the crown is where the mesh has it, behind the tip bone, at its full width
		struct V {
			float x, y, z;
			V operator+(const V& o) const { return { x + o.x, y + o.y, z + o.z }; }
			V operator-(const V& o) const { return { x - o.x, y - o.y, z - o.z }; }
			V operator*(float s) const { return { x * s, y * s, z * s }; }
		};
		struct P { V pos; float r; };
		// fo4-anatomy's profile; Penis_04 2.67 behind Penis_05 (the mesh), the head at x1.4: R = 1.8 x 1.4
		const std::vector<Glans::Step> prof = { { -0.85f, 0.90f }, { -0.60f, 1.00f }, { -0.35f, 0.93f },
			{ -0.12f, 0.81f }, { 0.0f, 0.73f } };
		const float skin = 0.2f, R = 2.52f;
		std::vector<P> chain = { { { -3.0f, 0, 0 }, 1.7f }, { { 0, 0, 0 }, 1.7f }, { { 2.67f, 0, 0 }, R } };
		Glans::Shape(chain, prof, skin);
		auto radiusAt = [&](float x) {             // as Mouth.cpp reads a chain: straight between points
			for (size_t k = 0; k + 1 < chain.size(); k++)
				if (chain[k].pos.x <= x && x <= chain[k + 1].pos.x) {
					float s = (x - chain[k].pos.x) / (chain[k + 1].pos.x - chain[k].pos.x);
					return chain[k].r + (chain[k + 1].r - chain[k].r) * s - skin;
				}
			return -1.0f;
		};
		float crown = 2.67f - 0.60f * R;
		std::printf("15: %d points; the flesh at the crown (%.2f) %.2f, the mesh's 2.52; the tip bone keeps %.2f\n",
			(int)chain.size(), crown, radiusAt(crown), chain.back().r - skin);
		Expect(chain.size() == 7, "15: the tip sphere becomes the profile's five points");
		Expect(std::fabs(radiusAt(crown) - R) < 0.01f, "15: at the crown the mouth sees the head's full width");
		Expect(std::fabs(chain.back().pos.x - 2.67f) < 1e-4f && std::fabs(chain.back().r - (0.73f * R + skin)) < 1e-4f,
			"15: the last point is still the tip bone (depth and the tip's cap unchanged), at the tip's own radius");
		std::vector<P> short1 = { { { 0, 0, 0 }, 1.7f }, { { 1.0f, 0, 0 }, R } };
		Glans::Shape(short1, prof, skin);
		bool ahead = true;                           // a segment 1.0 long: -0.85R and -0.60R lie behind its joint
		for (size_t k = 1; k < short1.size(); k++)
			ahead = ahead && short1[k].pos.x > 0.0f;
		Expect(short1.size() == 4 && ahead && short1.back().pos.x == 1.0f,
			"15: a step behind the joint before it is skipped (only the bone's own flesh)");
		std::vector<P> kept = { { { 0, 0, 0 }, 1.7f }, { { 2.67f, 0, 0 }, R } };
		Glans::Shape(kept, { { -0.6f, 1.0f } }, skin);   // does not end on the bone
		Expect(kept.size() == 2 && kept.back().r == R, "15: a profile that does not end on the bone changes nothing");
		std::vector<P> big = { { { 0, 0, 0 }, 1.7f }, { { 2.67f, 0, 0 }, 1.8f * 1.2f } };
		Glans::Shape(big, prof, skin);
		Expect(std::fabs(big[2].pos.x - (2.67f - 0.60f * 2.16f)) < 1e-4f, "15: a smaller head's crown sits closer to its bone");
	}
	if (failures) {
		std::printf("%d expectation(s) failed\n", failures);
		return 1;
	}
	std::printf("lips: every expectation holds\n");
	return 0;
}
