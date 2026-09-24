// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: the face written after the merge, tested outside the game.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
//
// Two parts. The unit part checks FaceAuthority (Rapport's messages, its face, the store). The frame
// part runs FaceCompose frame by frame, exactly as CBPSSE/Mouth.cpp HookMerge calls it, against a model
// of the engine's merge (Fallout4.exe 1.10.163 +0x6689D0 and the blink machine at +0x668170, as
// disassembled 2026-09-24; the model follows the one a review built for commit d4bff80). That is where
// the bugs that one-frame checks cannot see live: the engine reads its own weights back next frame.
//
// Run: tests\face\run.bat   (MSVC Build Tools 2022)
#include "FaceAuthority.h"
#include "FaceCompose.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace FaceAuthority;

static int failures = 0;
static void Check(const char* what, bool ok)
{
	printf("  %s %s\n", ok ? "OK " : "BAD", what);
	failures += !ok;
}
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// ---- the engine, as far as the merge goes --------------------------------------------------------
struct Engine
{
	float fin[kMorphs] = {};     // +0x18, what the mesh is built from
	float ovr[kMorphs] = {};     // +0xF0, MFG and a line's lip sync
	float anim[kMorphs] = {};    // +0x1C8, expression keyframes
	int state = 0;               // the blink machine: 0 resting, 1 closing, 2 opening
	float timer = 99.0f;         // no blink unless a check starts one
	bool speaking = false;       // the lip-sync object at +0x2C0 in state 3 or 4
	bool eyesClosedMode = false; // +0x2DB: the merge's loops are skipped

	void WriteEyes(float amount)
	{
		float v = (std::min)(1.0f, (std::max)(0.0f, amount));
		fin[18] = fin[41] = v;
	}
	void Blink(float dt)          // +0x668170, with its early exits
	{
		if (!(dt > 0.0f))
			return;
		switch (state) {
		case 0:
			if (speaking)
				return;               // bAllowBlinksDuringSpeech is off by default: no write at all
			if (timer > dt)
				timer -= dt;
			else {
				state = 1;
				timer = 0.1f;
			}
			WriteEyes(0.0f);          // resting: the eyelids are written every frame
			return;
		case 1:
			if (timer > dt) {
				timer -= dt;
				WriteEyes(1.0f - timer / 0.1f);
			}
			else {
				state = 2;
				timer = 0.1f;
				WriteEyes(1.0f);
			}
			return;
		default:
			if (timer > dt) {
				timer -= dt;
				WriteEyes(timer / 0.1f);
			}
			else {
				state = 0;
				timer = 99.0f;
				WriteEyes(0.0f);
			}
			return;
		}
	}
	bool Merge(float dt)          // +0x6689D0 (flag 1, the max path)
	{
		Blink(dt);
		float left = fin[18], right = fin[41];   // read from FINAL after the blink machine
		if (!(dt > 0.0f) || eyesClosedMode)
			return false;                        // nothing computed: the last final stays
		for (int i = 0; i < kMorphs; i++) {
			float x = (std::max)(ovr[i], anim[i]);
			fin[i] = (std::min)(1.0f, (std::max)(0.0f, x));
		}
		fin[18] = (std::min)(1.0f, left + anim[18]);
		fin[41] = (std::min)(1.0f, right + anim[41]);
		return true;
	}
};

// ---- the hook, as CBPSSE/Mouth.cpp HookMerge calls FaceCompose ------------------------------------
struct Hook
{
	FaceCompose::Engine last;    // Mouth.cpp's written[data].engine
	bool published = false;      // this face is in Mouth.cpp's published list
	bool held = false;
	Face face;
	FaceCompose::Mouth mouth;

	bool Frame(Engine& e, float dt)
	{
		FaceCompose::Engine restore = last;
		if (!published)
			last.has = false;                    // HookMerge forgets the face as it gives it back
		FaceCompose::BeforeMerge(e.fin, restore);
		bool changed = e.Merge(dt);
		if (!published)
			return changed || restore.has;
		FaceCompose::AfterMerge(e.fin, last, held ? &face : nullptr, e.speaking, mouth);
		return true;
	}
};

static Face RapportFace(float lid, float jaw)
{
	Face f;
	f.owned = (1ull << 50) - 1;                  // Rapport owns the whole table, 0-49
	f.value[18] = f.value[41] = lid;
	f.value[2] = jaw;
	return f;
}

static SetMessage MakeSet(std::uint32_t formID, std::uint64_t owned)
{
	SetMessage m{};
	m.version = kVersion;
	m.formID = formID;
	m.owned = owned;
	for (int i = 0; i < kMorphs; i++)
		m.value[i] = 0.01f * i;
	return m;
}

int main()
{
	const float dt = 1.0f / 60.0f;
	const std::uint64_t all = (1ull << 50) - 1;

	printf("decode\n");
	{
		SetMessage s = MakeSet(0x00115E9F, all);
		Decoded d = Decode(kSet, &s, sizeof(s));
		Check("a valid set is a Set for its form with its mask and values",
			d.command == Command::Set && d.formID == 0x00115E9F && d.face.owned == all &&
			Near(d.face.value[14], 0.14f) && !d.refused);
		d = Decode(kSet, &s, sizeof(s) - 4);
		Check("a short set is refused", d.command == Command::None && d.refused);
		SetMessage v0 = s;
		v0.version = 0;
		d = Decode(kSet, &v0, sizeof(v0));
		Check("a set of version 0 is refused", d.command == Command::None && d.refused);
		SetMessage v2 = s;
		v2.version = 2;
		d = Decode(kSet, &v2, sizeof(v2));
		Check("a set of a later version is read for the fields we know", d.command == Command::Set && !d.refused);
		SetMessage zero = s;
		zero.formID = 0;
		d = Decode(kSet, &zero, sizeof(zero));
		Check("a set for form 0 is refused", d.command == Command::None && d.refused);
		SetMessage nan = s;
		nan.value[7] = std::numeric_limits<float>::quiet_NaN();
		d = Decode(kSet, &nan, sizeof(nan));
		Check("a set with a NaN is refused", d.command == Command::None && d.refused);
		SetMessage none = s;
		none.owned = 1ull << kSpeakingBit;       // no morph owned, only the speaking bit
		d = Decode(kSet, &none, sizeof(none));
		Check("a set that owns no morph is a Clear for its form", d.command == Command::Clear && d.formID == 0x00115E9F);
		ClearMessage c{ kVersion, 0 };
		d = Decode(kClear, &c, sizeof(c));
		Check("a clear of form 0 is Clear everyone", d.command == Command::Clear && d.formID == 0 && !d.refused);
		d = Decode(kClear, &c, 4);
		Check("a short clear is refused", d.command == Command::None && d.refused);
		d = Decode(0x12345678, &s, sizeof(s));
		Check("another message type is ignored without complaint", d.command == Command::None && !d.refused);
	}

	printf("compose, one frame\n");
	{
		Face held;
		held.owned = all;
		for (int i = 0; i < kMorphs; i++)
			held.value[i] = 0.3f;
		float w[kMorphs];
		for (int i = 0; i < kMorphs; i++)
			w[i] = 0.9f;
		w[18] = 1.0f;
		w[41] = 0.1f;
		w[51] = 0.77f;
		Compose(w, held, false);
		Check("an owned morph takes Rapport's value, even below the merge", Near(w[2], 0.3f));
		Check("the blink still closes over a held look", Near(w[18], 1.0f));
		Check("an open eye takes the held look", Near(w[41], 0.3f));
		Check("a morph outside the mask keeps the merge", Near(w[51], 0.77f));
		for (int i = 0; i < kMorphs; i++)
			w[i] = 0.9f;
		Compose(w, held, true);
		Check("speaking (the engine's lip state): the jaw keeps the lip sync", Near(w[2], 0.9f));
		Check("speaking: a brow stays Rapport's", Near(w[14], 0.3f));
		Face bit = held;
		bit.owned |= 1ull << kSpeakingBit;
		for (int i = 0; i < kMorphs; i++)
			w[i] = 0.9f;
		Compose(w, bit, false);
		Check("the sender's speaking bit does the same", Near(w[2], 0.9f) && Near(w[14], 0.3f));
		Face line = held;
		for (int i = 0; i < 50; i++)
			if (IsMouth(i))
				line.owned &= ~(1ull << i);
		for (int i = 0; i < kMorphs; i++)
			w[i] = 0.6f;
		Compose(w, line, false);
		Check("Rapport's way (mouth bits cleared): the jaw and tongue keep the engine's",
			Near(w[2], 0.6f) && Near(w[49], 0.6f) && Near(w[14], 0.3f));
		int mouth = 0;
		for (int i = 0; i < 50; i++)
			mouth += IsMouth(i);
		Check("the mouth set is Rapport's MOUTH: 29 ids", mouth == 29);
		Face loud = held;
		loud.value[3] = 7.0f;
		loud.value[4] = -2.0f;
		Compose(w, loud, false);
		Check("values are clamped to 0..1", Near(w[3], 1.0f) && Near(w[4], 0.0f));
	}

	printf("the self-test's face\n");
	{
		SetMessage t = TestFace(0x14);
		Decoded d = Decode(kSet, &t, sizeof(t));
		Check("the test face is a valid set for its form", d.command == Command::Set && d.formID == 0x14);
		float w[kMorphs];
		for (int i = 0; i < kMorphs; i++)
			w[i] = 0.5f;
		w[2] = 0.9f;
		w[18] = 1.0f;
		w[41] = 0.0f;
		w[52] = 0.33f;
		Compose(w, d.face, false);
		Check("the jaw is held shut over an animation that opens it", w[2] == 0.0f);
		Check("brows up and a smile", w[3] == 1.0f && w[26] == 1.0f && w[14] == 1.0f && w[37] == 1.0f &&
			Near(w[17], 0.8f) && Near(w[40], 0.8f));
		Check("a blink still closes the eye; the other eyelid sits at 0.2", w[18] == 1.0f && Near(w[41], 0.2f));
		Check("past the table (50-53) the merge stands", Near(w[52], 0.33f));
	}

	printf("the order: Rapport's face, the contact mouth, A-26 (one frame)\n");
	{
		float w[kMorphs] = {};
		w[2] = 0.10f;
		FaceCompose::Engine keep;
		Face r = RapportFace(0.0f, 0.35f);
		FaceCompose::Mouth m;
		m.inside = 1.0f;
		m.jaw = 0.8f;
		m.termCount = 1;
		m.termId[0] = 14;
		m.termValue[0] = 0.9f;
		FaceCompose::AfterMerge(w, keep, &r, false, m);
		Check("full contact over a held jaw 0.35: the mouth opens to the fit (0.8)", Near(w[2], 0.8f));
		Check("A-26 raises nothing on a held face (brow stays Rapport's 0)", Near(w[14], 0.0f));
		Check("the engine's own weights are kept before ours go on", keep.has && Near(keep.weight[2], 0.10f));
		for (int i = 0; i < kMorphs; i++)
			w[i] = 0.0f;
		w[2] = 0.10f;
		m.inside = 0.5f;
		FaceCompose::AfterMerge(w, keep, &r, false, m);
		Check("half contact over a held jaw 0.35: 0.35 + (0.8 - 0.35) x 0.5", Near(w[2], 0.575f));
		for (int i = 0; i < kMorphs; i++)
			w[i] = 0.0f;
		FaceCompose::AfterMerge(w, keep, nullptr, false, m);
		Check("no held face: A-26 raises the brow to 0.9 x 0.5", Near(w[14], 0.45f));
	}

	printf("frames: the eyelids while a line plays (the blink machine does not write)\n");
	{
		Engine e;
		Hook h;
		h.published = h.held = true;
		float at61 = -1.0f;
		for (int f = 0; f < 120; f++) {
			e.speaking = f >= 30;
			h.face = RapportFace(f < 60 ? 1.0f : 0.45f, 0.15f);
			h.Frame(e, dt);
			if (f == 61)
				at61 = e.fin[18];
		}
		Check("Rapport lowers the eyelid mid-line: it goes to 0.45 the next frame (not stuck at 1.0)", Near(at61, 0.45f));
	}
	{
		Engine e;
		Hook h;
		h.published = h.held = true;
		h.face = RapportFace(1.0f, 0.15f);
		float at61 = -1.0f;
		for (int f = 0; f < 120; f++) {
			e.speaking = f >= 30;
			h.published = f < 60;                   // Rapport's Clear lands at frame 60
			h.Frame(e, dt);
			if (f == 61)
				at61 = e.fin[18];
		}
		Check("a Clear mid-line gives the eyelid back to the engine at once (0, not 1.0)", Near(at61, 0.0f));
	}
	{
		Engine e;
		Hook h;
		h.published = h.held = true;
		h.face = RapportFace(0.3f, 0.15f);
		float peak = 0.0f;
		e.timer = 0.2f;                             // a blink starts a little in
		for (int f = 0; f < 60; f++) {
			h.Frame(e, dt);
			peak = (std::max)(peak, e.fin[18]);
		}
		Check("not speaking: a blink still closes the eye over a held 0.3", Near(peak, 1.0f));
		Check("and the eye comes back to the held 0.3", Near(e.fin[18], 0.3f));
	}

	printf("frames: a paused game (the merge computes nothing)\n");
	{
		Engine e;
		Hook h;
		h.published = true;
		e.anim[2] = 0.10f;
		h.mouth.inside = 0.3f;
		h.mouth.jaw = 0.8f;
		h.mouth.funnel = 0.3f;
		h.Frame(e, dt);
		float running = e.fin[2];
		bool steady = true;
		for (int f = 0; f < 12; f++) {
			h.Frame(e, 0.0f);
			steady = steady && Near(e.fin[2], running);
		}
		Check("a contact blend does not feed on itself while paused (jaw stays 0.31)", Near(running, 0.31f) && steady);
	}
	{
		Engine e;
		Hook h;
		h.published = h.held = true;
		h.face = RapportFace(0.0f, 0.0f);
		e.anim[2] = 0.6f;
		h.Frame(e, dt);
		float shut = e.fin[2];
		h.Frame(e, 0.0f);
		h.published = false;                        // released while paused
		bool rebuilt = h.Frame(e, 0.0f);
		Check("a held jaw 0 over an animation's 0.6", Near(shut, 0.0f));
		Check("released while paused: the engine's 0.6 is back at once, and the mesh is rebuilt",
			Near(e.fin[2], 0.6f) && rebuilt);
	}
	{
		Engine e;
		Hook h;
		h.published = h.held = true;
		h.face = RapportFace(0.0f, 0.0f);
		e.anim[2] = 0.6f;
		h.Frame(e, dt);
		e.eyesClosedMode = true;
		h.published = false;
		h.Frame(e, dt);
		Check("released in the eyes-closed mode (loops skipped): the engine's face is back", Near(e.fin[2], 0.6f));
	}

	printf("frames: a line on a held face (the engine's lip state hands the mouth back)\n");
	{
		Engine e;
		Hook h;
		h.published = h.held = true;
		h.face = RapportFace(0.0f, 0.15f);
		e.speaking = true;
		e.ovr[2] = 0.7f;                            // the line's lip sync, in the MFG layer
		h.Frame(e, dt);
		float talking = e.fin[2];
		e.speaking = false;
		e.ovr[2] = 0.0f;
		h.Frame(e, dt);
		Check("while the line plays the lip sync opens the jaw (0.7)", Near(talking, 0.7f));
		Check("after it the jaw is Rapport's again (0.15)", Near(e.fin[2], 0.15f));
		Check("a brow stays Rapport's all along", Near(e.fin[14], 0.0f));
	}

	printf("store\n");
	{
		Face held = RapportFace(0.3f, 0.3f);
		Set(0x14, held);
		Set(0x00115E9F, held);
		Check("two held faces", Snapshot().size() == 2);
		Clear(0x14);
		Check("clearing one leaves the other", Snapshot().size() == 1 && Snapshot()[0].first == 0x00115E9F);
		Clear(0);
		Check("clearing form 0 lets go of everyone", Snapshot().empty());
	}

	printf(failures ? "FAILURES: %d\n" : "ALL OK\n", failures);
	return failures ? 1 : 0;
}
