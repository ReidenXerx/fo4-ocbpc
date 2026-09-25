// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: the order in which a face is written after the engine's merge.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once

#include "FaceAuthority.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Everything this plugin writes over the engine's merged face weights (BSFaceGenAnimationData + 0x18),
// as pure functions, so the order and the frame-to-frame behaviour run in a test outside the game
// (tests/face). This is the ONE place where the layers of a face meet; each layer's right is written
// here and nowhere else (decisions A-20, A-26, A-27):
//   0. the engine: its merge of MFG, lip sync and keyframes, and the blink;
//   1. the face Rapport holds (FaceAuthority::Compose): owned morphs replaced, the blink kept, the
//      mouth left to the line's lip sync while the actor speaks;
//   1b. the deep face (A-29): the ids Rapport masks blend from the held face toward its deep face by
//      `deep` (how deep the contact is), so a pleading face can frown as the shaft goes deep, a thing a
//      raise-only layer cannot do (the brows must come DOWN). Never a MOUTH id; the blink keeps its max;
//   2. the contact mouth (A-20): from whatever the jaw is now, open to what is inside, by `inside`;
//      only the jaw, the two funnels and the upper lip (2, 21, 22, 44, 46), or (A-32) the lips fitted
//      round it: those and Lower Lip Down / Up and Upper Lip Down (11, 12, 20, 34, 35, 43);
//   3. the face while the mouth is busy (A-26): only RAISES its own ids (never Rapport's MOUTH ids
//      nor the blink: AfterMerge skips them, the loader refuses them), by at most its terms, and only
//      as far as `inside`, so it
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
	constexpr int kLeftLipCornerOut = 8;           // not MOUTH ids (Rapport's smiles use them): the lip path may
	constexpr int kRightLipCornerOut = 31;         // write them, by inside, so only while something is inside
	constexpr int kLeftBlink = 18;
	constexpr int kRightBlink = 41;

	// The contact mouth's share of one face this frame (Mouth.cpp measures it)
	struct Mouth
	{
		float inside = 0.0f;                        // 0..1: how far the contact mouth has taken over
		float deep = 0.0f;                          // 0..1: how deep the contact is (A-29's blend)
		float jaw = 0.0f, floor = 0.0f, funnel = 0.0f, lift = 0.0f;
		int lipCount = 0;                           // A-32: the lips fitted round what is inside (LipFit);
		int lipId[kMaxTerms] = {};                  // when there are any, they replace jaw, funnel and lift
		float lipValue[kMaxTerms] = {};
		int termCount = 0;                          // A-26: raise termId toward termValue x inside
		int termId[kMaxTerms] = {};
		float termValue[kMaxTerms] = {};
		std::uint64_t glanceMask = 0;               // a glance's face (RFAX), eased by glanceWeight, after the
		float glanceFace[kMorphs] = {};             // held face and its deep blend, before the contact mouth
		float glanceWeight = 0.0f;
		float lidMax = 1.0f;                        // a glance (RFAG): the upper lids (18/41) at most this
		                                            // far down, last of all, over the blink too
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
	// Layer 3 never touches Rapport's MOUTH ids nor the blink, whatever its terms say.
	void AfterMerge(float* weights, Engine& keep, const FaceAuthority::Face* held, bool speaking,
		const Mouth& mouth, bool reactOverHeld);

	// What the hook keeps between merges, per face data it writes over, and when it lets go. The key
	// is the face data's ADDRESS, and a freed face's address can be handed to another actor's face,
	// so an entry must never outlive the face it was kept for:
	//   - a face no longer published keeps its entry for the one merge that gives it back (Before);
	//   - an entry published neither in this list nor the one before is dropped (Published): its
	//     face had a whole frame to merge and did not, so it is likely gone;
	//   - on a cell change, faces unload wholesale: everything not in the new list goes (Keep);
	//   - a published face kept for another actor (its form differs) is not put back.
	// Not thread-safe by itself: Mouth.cpp calls it under its publish lock.
	template <typename Extra>
	class Ledger
	{
	public:
		struct Entry
		{
			std::uint32_t formID = 0;
			Engine engine;
			Extra extra{};
		};

		// the engine's weights to put back before this face's merge (none: has is false)
		Engine Before(const void* face, bool published, std::uint32_t formID)
		{
			auto it = entries.find(face);
			if (it == entries.end())
				return Engine{};
			Engine e = it->second.engine;
			if (!published)
				entries.erase(it);                  // this merge gives the face back to the engine
			else if (it->second.formID != formID) {
				entries.erase(it);                  // another actor's face at a freed address
				return Engine{};
			}
			return e;
		}
		void After(const void* face, const Entry& entry) { entries[face] = entry; }
		void Published(const std::vector<const void*>& now)
		{
			std::unordered_set<const void*> next(now.begin(), now.end());
			for (auto it = entries.begin(); it != entries.end();)
				it = next.count(it->first) || last.count(it->first) ? std::next(it) : entries.erase(it);
			last.swap(next);
		}
		void Keep(const std::vector<const void*>& now)
		{
			std::unordered_set<const void*> next(now.begin(), now.end());
			for (auto it = entries.begin(); it != entries.end();)
				it = next.count(it->first) ? std::next(it) : entries.erase(it);
			last.swap(next);
		}
		void Clear()
		{
			entries.clear();
			last.clear();
		}
		const Entry* Find(const void* face) const
		{
			auto it = entries.find(face);
			return it == entries.end() ? nullptr : &it->second;
		}
		std::size_t Size() const { return entries.size(); }

	private:
		std::unordered_map<const void*, Entry> entries;
		std::unordered_set<const void*> last;    // the addresses published last time
	};
}
