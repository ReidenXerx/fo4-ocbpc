// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: the order in which a face is written after the engine's merge.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once

#include "FaceAuthority.h"

// Everything this plugin writes over the engine's merged face weights (BSFaceGenAnimationData + 0x18),
// as pure functions, so the order and the frame-to-frame behaviour run in a test outside the game
// (tests/face). This is the ONE place where the layers of a face meet; each layer's right is written
// here and nowhere else (decisions A-20, A-26, A-27):
//   0. the engine: its merge of MFG, lip sync and keyframes, and the blink;
//   1. the face Rapport holds (FaceAuthority::Compose): owned morphs replaced, the blink kept, the
//      mouth left to the line's lip sync while the actor speaks;
//   2. the contact mouth (A-20): from whatever the jaw is now, open to what is inside, by `inside`;
//      only the jaw, the two funnels and the upper lip (2, 21, 22, 44, 46);
//   3. the face while the mouth is busy (A-26): only RAISES its own ids (never the mouth's nor the
//      blink: the loader refuses them), by at most its terms, and only as far as `inside`, so it
//      comes and goes with the contact. On a face Rapport holds it rises above Rapport's values: the
//      owner, 2026-09-24, "if it will work smoothly and won't bite", so Rapport stays the base and
//      this is the one physical exception, like the mouth. [Face] react=0 takes it off held faces.
//
// The engine must never see what we wrote. It reads its own final weights back on the next frame:
// the eyelids always (the blink is added to them, and the blink machine does not write while a line
// plays), and every weight while the game is paused or the face is in its eyes-closed mode (the
// merge then computes nothing). Writing over our own last frame compounds: a blend feeds on itself,
// an eyelid can rise but never fall, a released face stays. So before each merge the weights go back
// to the engine's own (BeforeMerge), and after it the engine's are kept before ours go on (AfterMerge).
namespace FaceCompose
{
	constexpr int kMorphs = FaceAuthority::kMorphs;
	constexpr int kMaxTerms = 16;
	constexpr int kJawOpen = 2;
	constexpr int kLeftUpperLipUp = 21;
	constexpr int kLowerLipFunnel = 22;
	constexpr int kRightUpperLipUp = 44;
	constexpr int kUpperLipFunnel = 46;

	// The contact mouth's share of one face this frame (Mouth.cpp measures it)
	struct Mouth
	{
		float inside = 0.0f;                        // 0..1: how far the contact mouth has taken over
		float jaw = 0.0f, floor = 0.0f, funnel = 0.0f, lift = 0.0f;
		int termCount = 0;                          // A-26: raise termId toward termValue x inside
		int termId[kMaxTerms] = {};
		float termValue[kMaxTerms] = {};
	};

	// The engine's own weights from the last merge we wrote over
	struct Engine
	{
		bool has = false;
		float weight[kMorphs] = {};
	};

	// Before the engine's merge: its own last weights back where ours were
	void BeforeMerge(float* weights, const Engine& last);

	// After it: keep the engine's weights, then write ours. held is Rapport's face or null; speaking
	// is the engine's lip state (a line is playing); reactOverHeld lets layer 3 rise above a held face.
	void AfterMerge(float* weights, Engine& keep, const FaceAuthority::Face* held, bool speaking,
		const Mouth& mouth, bool reactOverHeld);
}
