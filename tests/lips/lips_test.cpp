// fo4-ocbpc: the lips around what is in her mouth (A-32), tested outside the game. Build and run: tests/lips/run.bat.
// The table is the female head's, as fo4-anatomy tools/lips.py measured it on the game's heads (2026-09-25).
#include "LipFit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
	struct Row { int id; float u[kSamples]; float l[kSamples]; };
	static const Row rows[] = {
		{ 2, { -0.523f, -0.164f, -0.066f, -0.066f, -0.075f, -0.156f, -0.367f }, { -1.457f, -1.734f, -1.888f, -1.910f, -1.895f, -1.746f, -1.509f } },
		{ 22, { 0.003f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.003f }, { -0.022f, -0.172f, -0.316f, -0.317f, -0.316f, -0.172f, -0.021f } },
		{ 46, { 0.005f, 0.145f, 0.179f, 0.171f, 0.177f, 0.154f, 0.033f }, { 0.0f, 0.002f, 0.0f, 0.0f, 0.0f, 0.001f, 0.0f } },
		{ 21, { 0.37f, 0.38f, 0.34f, 0.23f, 0.12f, 0.04f, 0.0f }, { 0.01f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
		{ 44, { 0.0f, 0.04f, 0.12f, 0.23f, 0.34f, 0.38f, 0.38f }, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.01f } },
		{ 11, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { -0.32f, -0.39f, -0.33f, -0.22f, -0.13f, -0.05f, -0.01f } },
		{ 34, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, { -0.01f, -0.05f, -0.13f, -0.22f, -0.33f, -0.39f, -0.33f } },
		{ 20, { -0.24f, -0.28f, -0.26f, -0.18f, -0.10f, -0.03f, 0.0f }, { 0.02f, -0.05f, -0.07f, -0.07f, -0.07f, -0.05f, 0.01f } },
		{ 43, { 0.0f, -0.03f, -0.10f, -0.18f, -0.26f, -0.28f, -0.25f }, { 0.02f, -0.05f, -0.07f, -0.07f, -0.07f, -0.05f, 0.01f } },
		{ 12, { 0.03f, 0.03f, 0.04f, 0.05f, 0.04f, 0.03f, 0.03f }, { 0.29f, 0.30f, 0.25f, 0.17f, 0.10f, 0.04f, 0.0f } },
		{ 35, { 0.03f, 0.03f, 0.04f, 0.05f, 0.04f, 0.03f, 0.03f }, { 0.0f, 0.04f, 0.10f, 0.17f, 0.25f, 0.30f, 0.29f } },
	};
	Table t;
	for (const Row& r : rows) {
		int m = t.count++;
		t.id[m] = r.id;
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
	{   // 10. the corners: the female head's rim (-1.60 .. 1.59) and Corner Out's moves (0.40 left, 0.34 right)
		const float rim[2] = { -1.60f, 1.59f }, move[2] = { 0.40f, 0.34f };
		float l, r;
		Corners(-1.2f, 1.2f, rim, move, 0.05f, l, r);
		Expect(l == 0.0f && r == 0.0f, "10: a shaft inside the corners leaves them be");
		Corners(-1.75f, 1.64f, rim, move, 0.05f, l, r);
		Expect(std::fabs(l - 0.5f) < 1e-4f && std::fabs(r - 0.2941f) < 1e-3f,
			"10: past a corner, it goes out by the missing distance over its move (left 0.20/0.40, right 0.10/0.34)");
		Corners(-3.0f, 3.0f, rim, move, 0.05f, l, r);
		Expect(l == 1.0f && r == 1.0f, "10: far too wide: both corners out as far as they go, not past");
		Corners(-1.3f, 1.7f, rim, move, 0.05f, l, r);
		Expect(l == 0.0f && r > 0.4f, "10: off to the right: only the right corner goes out");
	}

	if (failures) {
		std::printf("%d expectation(s) failed\n", failures);
		return 1;
	}
	std::printf("lips: every expectation holds\n");
	return 0;
}
