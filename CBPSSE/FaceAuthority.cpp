// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: face authority, Rapport's faces applied after the merge.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "FaceAuthority.h"

#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace FaceAuthority
{
	namespace
	{
		std::mutex lock;
		std::unordered_map<std::uint32_t, Face> held;

		// The mouth handed back to the engine while an actor speaks: Rapport's own MOUTH set
		// (fo4-rapport tools/make_mfg.py), a superset of whatever lip sync drives. Ids are the
		// engine's 50-morph expression table: the jaw (1 2 6 29), both funnels (22 46), the rolls
		// (23 24 47 48), pucker (25), sticky lips (45), the tongue (49), and per side the lip corners
		// in/out, lower lip down/up, upper lip down/up, smile and frown.
		constexpr int kMouth[] = { 1, 2, 6, 29, 22, 46, 23, 24, 47, 48, 25, 45, 49,
			7, 8, 11, 12, 20, 21, 17, 5,
			30, 31, 34, 35, 43, 44, 40, 28 };
		constexpr int kLeftBlink = 18, kRightBlink = 41;
	}

	bool IsMouth(int id)
	{
		for (int m : kMouth)
			if (m == id)
				return true;
		return false;
	}

	Decoded Decode(std::uint32_t type, const void* data, std::uint32_t length)
	{
		Decoded d;
		if (type != kSet && type != kClear)
			return d;                                   // not ours to read
		if (!data) {
			d.refused = "no data";
			return d;
		}
		if (type == kSet) {
			if (length < sizeof(SetMessage)) {
				d.refused = "a set shorter than 232 bytes";
				return d;
			}
			SetMessage m;
			std::memcpy(&m, data, sizeof(m));
			if (m.version < kVersion) {
				d.refused = "a set of version 0";       // a later version only appends: read what we know
				return d;
			}
			if (m.formID == 0) {
				d.refused = "a set for form 0";
				return d;
			}
			for (int i = 0; i < kMorphs; i++)
				if (!std::isfinite(m.value[i])) {
					d.refused = "a set with a value that is not a number";
					return d;
				}
			d.formID = m.formID;
			if (!(m.owned & ((1ull << kMorphs) - 1))) {
				d.command = Command::Clear;             // a face that holds no morph holds nothing
				return d;
			}
			d.command = Command::Set;
			d.face.owned = m.owned;
			std::memcpy(d.face.value, m.value, sizeof(m.value));
			return d;
		}
		if (length < sizeof(ClearMessage)) {
			d.refused = "a clear shorter than 8 bytes";
			return d;
		}
		ClearMessage m;
		std::memcpy(&m, data, sizeof(m));
		if (m.version < kVersion) {
			d.refused = "a clear of version 0";
			return d;
		}
		d.command = Command::Clear;
		d.formID = m.formID;
		return d;
	}

	void Compose(float* weights, const Face& face, bool speaking)
	{
		speaking = speaking || ((face.owned >> kSpeakingBit) & 1u);
		for (int i = 0; i < kMorphs; i++) {
			if (!((face.owned >> i) & 1u))
				continue;                               // not Rapport's: the merge stands
			if (speaking && IsMouth(i))
				continue;                               // the line's lip sync
			float v = face.value[i] < 0.0f ? 0.0f : (face.value[i] > 1.0f ? 1.0f : face.value[i]);
			if (i == kLeftBlink || i == kRightBlink)
				weights[i] = weights[i] > v ? weights[i] : v;
			else
				weights[i] = v;
		}
	}

	SetMessage TestFace(std::uint32_t formID)
	{
		SetMessage m{};
		m.version = kVersion;
		m.formID = formID;
		m.owned = (1ull << 50) - 1;                     // the whole expression table, 0-49
		for (int id : { 3, 26, 14, 37 })               // outer and middle brows up
			m.value[id] = 1.0f;
		m.value[17] = m.value[40] = 0.8f;               // smile
		m.value[kLeftBlink] = m.value[kRightBlink] = 0.2f;
		m.value[2] = 0.0f;                              // Jaw Open: held shut
		return m;
	}

	void Set(std::uint32_t formID, const Face& face)
	{
		std::lock_guard<std::mutex> guard(lock);
		held[formID] = face;
	}

	void Clear(std::uint32_t formID)
	{
		std::lock_guard<std::mutex> guard(lock);
		if (formID == 0)
			held.clear();
		else
			held.erase(formID);
	}

	std::vector<std::pair<std::uint32_t, Face>> Snapshot()
	{
		std::lock_guard<std::mutex> guard(lock);
		return std::vector<std::pair<std::uint32_t, Face>>(held.begin(), held.end());
	}
}
