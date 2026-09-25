// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: the order in which a face is written after the engine's merge.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "FaceCompose.h"

#include <algorithm>
#include <cstring>

namespace FaceCompose
{
	void BeforeMerge(float* weights, const Engine& last)
	{
		if (last.has)
			std::memcpy(weights, last.weight, sizeof(last.weight));
	}

	void AfterMerge(float* w, Engine& keep, const FaceAuthority::Face* held, bool speaking, const Mouth& m,
		bool reactOverHeld)
	{
		std::memcpy(keep.weight, w, sizeof(keep.weight));
		keep.has = true;
		if (held) {
			FaceAuthority::Compose(w, *held, speaking);
			if (held->deepMask)
				FaceAuthority::BlendDeep(w, keep.weight, *held, m.deep);   // 1b: the deep face, by depth
		}
		// 1c, a glance's face (RFAX): toward it by the glance's ease. Its MOUTH ids only as far as no contact
		// mouth has the mouth, and never over a line's lip sync; the lids are the glance's lids layer's
		if (m.glanceWeight > 0.0f && m.glanceMask) {
			bool quiet = speaking || (held && ((held->owned >> FaceAuthority::kSpeakingBit) & 1u));
			for (int id = 0; id < kMorphs; id++) {
				if (!((m.glanceMask >> id) & 1u) || id == kLeftBlink || id == kRightBlink)
					continue;
				float wt = m.glanceWeight > 1.0f ? 1.0f : m.glanceWeight;
				// an id the deep face authors stays the deep face's as far as the contact is deep (the owner,
				// 2026-09-26: "deep brows reactions disappeared during blowjob" - a 4-7 s glance wore them away)
				if (held && ((held->deepMask >> id) & 1u))
					wt *= 1.0f - (m.deep < 0.0f ? 0.0f : (m.deep > 1.0f ? 1.0f : m.deep));
				if (FaceAuthority::IsMouth(id)) {
					if (quiet)
						continue;
					wt *= 1.0f - (m.inside < 0.0f ? 0.0f : (m.inside > 1.0f ? 1.0f : m.inside));
				}
				float v = m.glanceFace[id] < 0.0f ? 0.0f : (m.glanceFace[id] > 1.0f ? 1.0f : m.glanceFace[id]);
				w[id] += (v - w[id]) * wt;
			}
		}
		if (m.lipCount > 0) {                       // A-32: the fitted lips, each by inside
			for (int k = 0; k < m.lipCount && k < kMaxTerms; k++) {
				int id = m.lipId[k];
				if (id == kLeftLipCornerOut || id == kRightLipCornerOut)
					w[id] = (std::max)(w[id], m.lipValue[k] * m.inside);   // a corner only ever goes OUT: a
					                                    // smile Rapport holds stays, the contact adds room to it
				else if (id >= 0 && id < kMorphs && FaceAuthority::IsMouth(id))
					w[id] += (m.lipValue[k] - w[id]) * m.inside;
			}
			w[kJawOpen] = (std::max)(w[kJawOpen], m.floor);
		}
		else {
			float jaw = w[kJawOpen] + (m.jaw - w[kJawOpen]) * m.inside;
			w[kJawOpen] = (std::max)(jaw, m.floor);
			w[kLowerLipFunnel] += (m.funnel - w[kLowerLipFunnel]) * m.inside;
			w[kUpperLipFunnel] += (m.funnel - w[kUpperLipFunnel]) * m.inside;
			w[kLeftUpperLipUp] += (m.lift - w[kLeftUpperLipUp]) * m.inside;
			w[kRightUpperLipUp] += (m.lift - w[kRightUpperLipUp]) * m.inside;
		}
		// [Face] react=0: Rapport's face alone, no layer 3
		for (int k = 0; !(held && !reactOverHeld) && k < m.termCount && k < kMaxTerms; k++) {
			int id = m.termId[k];
			if (id < 0 || id >= kMorphs || FaceAuthority::IsMouth(id) || id == kLeftBlink || id == kRightBlink)
				continue;                           // layer 3's right: brows, cheeks, nose; never the mouth
			if (held && ((held->deepMask >> id) & 1u))
				continue;                           // a deep face authors this id: one author, not two
			w[id] = (std::max)(w[id], m.termValue[k] * m.inside);   // nor the blink
		}
		// layer 4, a glance: eyes that look into his are open, whatever the face and the blink say
		if (m.lidMax < 1.0f) {
			float cap = m.lidMax < 0.0f ? 0.0f : m.lidMax;
			w[kLeftBlink] = (std::min)(w[kLeftBlink], cap);
			w[kRightBlink] = (std::min)(w[kRightBlink], cap);
		}
	}
}
