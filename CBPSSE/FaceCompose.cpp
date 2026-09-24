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
		if (m.lipCount > 0) {                       // A-32: the fitted lips, each by inside
			for (int k = 0; k < m.lipCount && k < kMaxTerms; k++) {
				int id = m.lipId[k];
				if (id >= 0 && id < kMorphs && FaceAuthority::IsMouth(id))
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
		if (held && !reactOverHeld)
			return;                                 // [Face] react=0: Rapport's face alone
		for (int k = 0; k < m.termCount && k < kMaxTerms; k++) {
			int id = m.termId[k];
			if (id < 0 || id >= kMorphs || FaceAuthority::IsMouth(id) || id == kLeftBlink || id == kRightBlink)
				continue;                           // layer 3's right: brows, cheeks, nose; never the mouth
			if (held && ((held->deepMask >> id) & 1u))
				continue;                           // a deep face authors this id: one author, not two
			w[id] = (std::max)(w[id], m.termValue[k] * m.inside);   // nor the blink
		}
	}
}
