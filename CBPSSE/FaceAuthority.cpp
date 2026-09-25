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
		Knobs current;                                  // RFAK, once heard (a load keeps them: they are settings)
		bool knobsHeard = false;
		std::unordered_map<std::uint32_t, Running> glances;   // RFAG, by looker
		struct Pending
		{
			std::uint64_t mask = 0;
			float value[kMorphs] = {};
			std::uint64_t atMs = 0;
		};
		std::unordered_map<std::uint32_t, Pending> glanceFaces;   // RFAX waiting for its RFAG, by looker

		// The mouth handed back to the engine while an actor speaks: Rapport's own MOUTH set (fo4-rapport
		// faces.json "mouth"). Measured 2026-09-24 by [Face] probe on 14 lines: the ids lip sync moved,
		// minus the six Rapport attributes to a face reaction (3/26, 4/27, 14/37). Ids are the engine's
		// 50-morph expression table. Dropped from the first guess, never moved by a line: 5/28 (frown),
		// 6/29 (jaw sideways), 8/31 (lip corner out). 23 ids.
		constexpr int kMouth[] = { 1, 2, 7, 11, 12, 17, 20, 21, 22, 23, 24, 25,
			30, 34, 35, 40, 43, 44, 45, 46, 47, 48, 49 };
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
		if (type != kSet && type != kClear && type != kDeep && type != kKnobs && type != kGlance && type != kGlanceFace)
			return d;                                   // not ours to read
		if (!data) {
			d.refused = "no data";
			return d;
		}
		if (type == kGlanceFace) {
			if (length < sizeof(SetMessage)) {
				d.refused = "a glance face shorter than 232 bytes";
				return d;
			}
			SetMessage m;
			std::memcpy(&m, data, sizeof(m));
			if (m.version < 1) {
				d.refused = "a glance face of version 0";
				return d;
			}
			if (m.formID == 0) {
				d.refused = "a glance face for form 0";
				return d;
			}
			for (int i = 0; i < kMorphs; i++)
				if (!std::isfinite(m.value[i])) {
					d.refused = "a glance face with a value that is not a number";
					return d;
				}
			// the blink lids are the glance's lids layer's
			std::uint64_t mask = m.owned & ((1ull << kMorphs) - 1) & ~((1ull << 18) | (1ull << 41));
			if (!mask) {
				d.refused = "a glance face that holds nothing";
				return d;
			}
			d.command = Command::GlanceFace;
			d.formID = m.formID;
			d.face.owned = mask;
			for (int i = 0; i < kMorphs; i++)
				d.face.value[i] = m.value[i] < 0.0f ? 0.0f : (m.value[i] > 1.0f ? 1.0f : m.value[i]);
			return d;
		}
		if (type == kGlance) {
			if (length < sizeof(GlanceMessage)) {
				d.refused = "a glance shorter than 24 bytes";
				return d;
			}
			GlanceMessage m;
			std::memcpy(&m, data, sizeof(m));
			if (m.version < 1) {
				d.refused = "a glance of version 0";
				return d;
			}
			if (m.looker == 0) {
				d.refused = "a glance by form 0";
				return d;
			}
			if (!std::isfinite(m.lidsOpen)) {
				d.refused = "a glance whose lids are not a number";
				return d;
			}
			const std::uint32_t flags = m.flags & kGlanceRoll;
			if (m.target == m.looker && !(flags & kGlanceRoll)) {
				d.refused = "a glance into one's own eyes";
				return d;
			}
			d.command = Command::Glance;
			d.formID = m.looker;
			d.glance.target = m.target;
			d.glance.durationMs = m.durationMs < 100 ? 100 : (m.durationMs > 10000 ? 10000 : m.durationMs);
			d.glance.lidsOpen = m.lidsOpen < 0.0f ? 0.0f : (m.lidsOpen > 1.0f ? 1.0f : m.lidsOpen);
			d.glance.flags = flags;
			return d;
		}
		if (type == kKnobs) {
			if (length < sizeof(KnobsMessage)) {
				d.refused = "knobs shorter than 32 bytes";
				return d;
			}
			KnobsMessage m;
			std::memcpy(&m, data, sizeof(m));
			if (m.version < 1) {
				d.refused = "knobs of version 0";
				return d;
			}
			const float v[6] = { m.lipClearance, m.lipSpeed, m.shaftScale, m.headMin, m.headMax, m.reactScale };
			for (float x : v)
				if (!std::isfinite(x)) {
					d.refused = "knobs with a value that is not a number";
					return d;
				}
			auto clamp = [](float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); };
			d.command = Command::Knobs;
			d.knobs.enabled = m.enabled & 0x1F;
			d.knobs.lipClearance = clamp(m.lipClearance, 0.0f, 0.5f);
			d.knobs.lipSpeed = clamp(m.lipSpeed, 0.1f, 5.0f);
			d.knobs.shaftScale = clamp(m.shaftScale, 0.5f, 1.5f);   // a typo must not deform him
			d.knobs.headMin = clamp(m.headMin, 0.5f, 2.0f);
			d.knobs.headMax = clamp(m.headMax, d.knobs.headMin, 2.0f);
			d.knobs.reactScale = clamp(m.reactScale, 0.0f, 3.0f);
			return d;
		}
		if (type == kSet || type == kDeep) {
			bool deep = type == kDeep;
			if (length < sizeof(SetMessage)) {
				d.refused = deep ? "a deep face shorter than 232 bytes" : "a set shorter than 232 bytes";
				return d;
			}
			SetMessage m;
			std::memcpy(&m, data, sizeof(m));
			if (m.version < kVersion) {
				d.refused = deep ? "a deep face of version 0" : "a set of version 0";   // later versions append
				return d;
			}
			if (m.formID == 0) {
				d.refused = deep ? "a deep face for form 0" : "a set for form 0";
				return d;
			}
			for (int i = 0; i < kMorphs; i++)
				if (!std::isfinite(m.value[i])) {
					d.refused = deep ? "a deep face with a value that is not a number"
					                 : "a set with a value that is not a number";
					return d;
				}
			d.formID = m.formID;
			if (deep) {                                 // a mask of nothing drops the deep face
				d.command = Command::Deep;
				d.face.deepMask = m.owned & ((1ull << kMorphs) - 1);
				std::memcpy(d.face.deep, m.value, sizeof(m.value));
				return d;
			}
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

	void BlendDeep(float* weights, const float* engine, const Face& face, float w)
	{
		w = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
		for (int i = 0; i < kMorphs; i++) {
			if (!((face.deepMask >> i) & 1u) || IsMouth(i))
				continue;
			float to = face.deep[i] < 0.0f ? 0.0f : (face.deep[i] > 1.0f ? 1.0f : face.deep[i]);
			if (i == kLeftBlink || i == kRightBlink) {
				// the held face's own lid (Compose has already taken the max with the blink), blended, and
				// then the blink's max again: the eye still closes over a deep look
				float from = ((face.owned >> i) & 1u) ? face.value[i] : engine[i];
				from = from < 0.0f ? 0.0f : (from > 1.0f ? 1.0f : from);
				float v = from + (to - from) * w;
				weights[i] = engine[i] > v ? engine[i] : v;
			}
			else
				weights[i] += (to - weights[i]) * w;
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
		Face f = face;
		f.easeMask = 0;
		held[formID] = f;
	}

	void Shown(const Face& face, std::uint64_t nowMs, float* out)
	{
		float t = nowMs > face.easeStartMs ? (float)(nowMs - face.easeStartMs) / (float)kEaseMs : 0.0f;
		t = t > 1.0f ? 1.0f : t;
		for (int i = 0; i < kMorphs; i++) {
			out[i] = face.value[i];
			if ((face.easeMask >> i) & 1u)
				out[i] = face.from[i] + (face.value[i] - face.from[i]) * t;
		}
	}

	void Set(std::uint32_t formID, const Face& face, std::uint64_t nowMs)
	{
		std::lock_guard<std::mutex> guard(lock);
		Face f = face;
		f.easeMask = 0;
		auto it = held.find(formID);
		if (it != held.end()) {
			float shown[kMorphs];
			Shown(it->second, nowMs, shown);
			f.easeMask = it->second.owned & f.owned & ((1ull << kMorphs) - 1);
			f.easeStartMs = nowMs;
			for (int i = 0; i < kMorphs; i++)
				f.from[i] = shown[i];
		}
		held[formID] = f;
	}

	void Clear(std::uint32_t formID)
	{
		std::lock_guard<std::mutex> guard(lock);
		if (formID == 0) {
			held.clear();
			glances.clear();                            // a load: nobody is looking at anybody any more
			glanceFaces.clear();
		}
		else
			held.erase(formID);
	}

	bool SetDeep(std::uint32_t formID, std::uint64_t mask, const float* values)
	{
		std::lock_guard<std::mutex> guard(lock);
		auto it = held.find(formID);
		if (it == held.end())
			return false;                               // a deep face belongs to a held one
		it->second.deepMask = mask;
		std::memcpy(it->second.deep, values, sizeof(it->second.deep));
		return true;
	}

	std::vector<std::pair<std::uint32_t, Face>> Snapshot()
	{
		std::lock_guard<std::mutex> guard(lock);
		return std::vector<std::pair<std::uint32_t, Face>>(held.begin(), held.end());
	}

	std::vector<std::pair<std::uint32_t, Face>> Snapshot(std::uint64_t nowMs)
	{
		std::lock_guard<std::mutex> guard(lock);
		std::vector<std::pair<std::uint32_t, Face>> out(held.begin(), held.end());
		for (auto& h : out) {
			float shown[kMorphs];
			Shown(h.second, nowMs, shown);
			std::memcpy(h.second.value, shown, sizeof(shown));
			h.second.easeMask = 0;
		}
		return out;
	}

	void SetKnobs(const Knobs& knobs)
	{
		std::lock_guard<std::mutex> guard(lock);
		current = knobs;
		knobsHeard = true;
	}

	bool CurrentKnobs(Knobs& out)
	{
		std::lock_guard<std::mutex> guard(lock);
		out = current;
		return knobsHeard;
	}

	void SetGlance(std::uint32_t looker, const Glance& glance, std::uint64_t nowMs)
	{
		std::lock_guard<std::mutex> guard(lock);
		auto pending = glanceFaces.find(looker);
		if (glance.target == 0) {
			glances.erase(looker);
			if (pending != glanceFaces.end())
				glanceFaces.erase(pending);
			return;
		}
		Running r;
		r.looker = looker;
		r.glance = glance;
		r.startMs = nowMs;
		if (pending != glanceFaces.end()) {
			if (nowMs >= pending->second.atMs && nowMs - pending->second.atMs <= kGlanceFaceWaitMs) {
				r.faceMask = pending->second.mask;
				std::memcpy(r.face, pending->second.value, sizeof(r.face));
			}
			glanceFaces.erase(pending);                 // an RFAX is for the next glance only
		}
		glances[looker] = r;
	}

	void SetGlanceFace(std::uint32_t looker, std::uint64_t mask, const float* values, std::uint64_t nowMs)
	{
		std::lock_guard<std::mutex> guard(lock);
		Pending p;
		p.mask = mask;
		std::memcpy(p.value, values, sizeof(p.value));
		p.atMs = nowMs;
		glanceFaces[looker] = p;
	}

	float GlanceFaceWeight(std::uint64_t nowMs, std::uint64_t startMs, std::uint32_t durationMs)
	{
		if (nowMs < startMs || nowMs >= startMs + durationMs || durationMs == 0)
			return 0.0f;
		float in = 150.0f, out = 250.0f;
		if (durationMs < 400) {
			in = 150.0f * durationMs / 400.0f;
			out = 250.0f * durationMs / 400.0f;
		}
		float a = (float)(nowMs - startMs) / in, b = (float)(startMs + durationMs - nowMs) / out;
		float e = (a < 1.0f ? a : 1.0f) * (b < 1.0f ? b : 1.0f);
		return e < 0.0f ? 0.0f : (e > 1.0f ? 1.0f : e);
	}

	std::vector<Running> Glances(std::uint64_t nowMs)
	{
		std::lock_guard<std::mutex> guard(lock);
		std::vector<Running> out;
		for (auto it = glances.begin(); it != glances.end();) {
			if (nowMs >= it->second.startMs + it->second.glance.durationMs) {
				it = glances.erase(it);             // done
				continue;
			}
			out.push_back(it->second);
			++it;
		}
		return out;
	}
}
