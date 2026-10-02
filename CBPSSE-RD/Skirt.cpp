// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-01: skirt bones (roadmap 5).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Skirt.h"

#include "ActorUtils.h"
#include "Bones.h"
#include "Game.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	struct Params
	{
		bool enabled = true;
		int columns = 12, levels = 5, iterations = 3;
		std::string prefix = "AnatSkirt_";
		std::vector<float> stiffness;
		float damping = 0.12f, hold = 4.0f, stretch = 1.05f, squash = 0.9f, spread = 1.6f, hintUp = 0.5f,
			  reset = 60.0f, step = 1.0f / 60.0f, maxStep = 2.0f, gravity = 0.1f, deep = 0.75f, cross = 0.5f, away = 0.2f;
	} P;
	// per sex: each bone's clearance from each leg capsule (L thigh, L calf, R thigh, R calf), then from its segment up
	std::unordered_map<std::string, std::vector<float>> clearF, clearM;
	const char* kLegs[2][3] = { { "LLeg_Thigh", "LLeg_Calf", "LLeg_Foot" }, { "RLeg_Thigh", "RLeg_Calf", "RLeg_Foot" } };

	struct State
	{
		std::vector<NiPoint3> p, prev;
		std::chrono::steady_clock::time_point last;
		bool has = false;
	};
	std::unordered_map<UInt32, State> states;
	std::mutex lock;

	float Dot(const NiPoint3& a, const NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	float Len(const NiPoint3& a) { return std::sqrt(Dot(a, a)); }

	std::vector<float> Floats(const std::string& text)
	{
		std::vector<float> out;
		std::stringstream in(text);
		std::string part;
		while (std::getline(in, part, ','))
			out.push_back(strtof(part.c_str(), nullptr));
		return out;
	}

	std::string LowerOf(std::string s)
	{
		for (auto& ch : s)
			ch = (char)tolower((unsigned char)ch);
		return s;
	}

	std::string Name(int c, int l)
	{
		char buf[64];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%02d_%d", P.prefix.c_str(), c, l);
		return buf;
	}

	// closest points between segments p0-p1 and q0-q1 (tools/skirt.py _closest_segments)
	void ClosestSegments(const NiPoint3& p0, const NiPoint3& p1, const NiPoint3& q0, const NiPoint3& q1, NiPoint3& onP,
		NiPoint3& onQ)
	{
		const NiPoint3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
		const float a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
		float s = 0.0f, t = 0.0f;
		auto clamp01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
		if (a <= 1e-12f) {
			t = e > 1e-12f ? clamp01(f / e) : 0.0f;
		} else {
			const float c = Dot(d1, r);
			if (e <= 1e-12f) {
				s = clamp01(-c / a);
			} else {
				const float b = Dot(d1, d2), den = a * e - b * b;
				s = den > 1e-12f ? clamp01((b * f - c * e) / den) : 0.0f;
				t = (b * s + f) / e;
				if (t < 0.0f) {
					t = 0.0f;
					s = clamp01(-c / a);
				} else if (t > 1.0f) {
					t = 1.0f;
					s = clamp01((b - c) / a);
				}
			}
		}
		onP = p0 + d1 * s;
		onQ = q0 + d2 * t;
	}

	void Note(const std::string& key, const char* fmt, ...)
	{
		static std::unordered_map<std::string, bool> said;
		if (said[key])
			return;
		said[key] = true;
		char buf[512];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		spdlog::info("{}", buf);
	}

	void UpdateImpl(Actor* actor)
	{
		NiAVObject* root = G::Root(actor);
		if (!root)
			return;
		BSFixedString pelvisName("Pelvis_skin");
		NiAVObject* pelvis = root->GetObjectByName(pelvisName);
		if (!pelvis)
			return;
		const int nc = P.columns, nl = P.levels, n = nc * nl;
		std::vector<NiAVObject*> nodes(n, nullptr);
		int found = 0;
		for (int c = 0; c < nc; c++)
			for (int l = 0; l < nl; l++) {
				BSFixedString want(Name(c, l).c_str());
				NiAVObject* node = pelvis->GetObjectByName(want);
				if (node && G::Parent(node) == pelvis) {
					nodes[c * nl + l] = node;
					found++;
				}
			}
		if (found == 0)
			return;                                     // no garment of hers names them: nothing to move
		const bool male = actorUtils::IsActorMale(actor);
		auto& clearTable = male && !clearM.empty() ? clearM : clearF;
		std::vector<NiPoint3> rest(n);
		std::vector<std::vector<float>> clear(n);
		for (int i = 0; i < n; i++) {
			const std::string nm = Name(i / nl, i % nl);
			auto it = clearTable.find(LowerOf(nm));   // the INI reader may lowercase keys
			if (!AnatomyBoneRest(nm, male, rest[i]) || it == clearTable.end() || it->second.size() != 8) {
				Note("skirt|table|" + nm, "[skirt] %s has no rest or clearance row: the skirt is off\n", nm.c_str());
				return;
			}
			clear[i] = it->second;
		}
		// legs in Pelvis_skin's frame (local = R (world - pos) / scale, the inverse of Aim's UpdateWorldFrom)
		const NiTransform& pw = G::World(pelvis);
		const float scale = pw.scale > 1e-6f ? pw.scale : 1.0f;
		auto toLocal = [&](const NiPoint3& w) { return (pw.rot * (w - pw.pos)) / scale; };
		NiPoint3 capA[4], capB[4];
		for (int sd = 0; sd < 2; sd++) {
			NiAVObject* j[3];
			for (int k = 0; k < 3; k++) {
				BSFixedString jn(kLegs[sd][k]);
				j[k] = root->GetObjectByName(jn);
				if (!j[k])
					return;
			}
			capA[sd * 2] = toLocal(G::World(j[0]).pos);
			capB[sd * 2] = toLocal(G::World(j[1]).pos);
			capA[sd * 2 + 1] = toLocal(G::World(j[1]).pos);
			capB[sd * 2 + 1] = toLocal(G::World(j[2]).pos);
		}
		NiPoint3 up = pw.rot * NiPoint3(0.0f, 0.0f, 1.0f);
		up = up / (Len(up) > 1e-6f ? Len(up) : 1.0f);
		// each bone's outward direction: from its level's centre (the ring's mean) to it
		std::vector<NiPoint3> hint(n);
		for (int l = 0; l < nl; l++) {
			NiPoint3 ctr;
			for (int c = 0; c < nc; c++)
				ctr += rest[c * nl + l];
			ctr = ctr / (float)nc;
			for (int c = 0; c < nc; c++) {
				NiPoint3 v = rest[c * nl + l] - ctr;
				const float vl = Len(v);
				hint[c * nl + l] = (vl > 1e-6f ? v / vl : v) + up * P.hintUp;
			}
		}
		std::vector<float> chain(n, 0.0f), ring(n, 0.0f);
		for (int c = 0; c < nc; c++)
			for (int l = 0; l < nl; l++) {
				if (l)
					chain[c * nl + l] = Len(rest[c * nl + l] - rest[c * nl + l - 1]);
				ring[c * nl + l] = Len(rest[((c + 1) % nc) * nl + l] - rest[c * nl + l]);
			}

		std::lock_guard<std::mutex> guard(lock);
		State& st = states[actor->formID];
		const auto now = std::chrono::steady_clock::now();
		float dt = st.has ? std::chrono::duration<float>(now - st.last).count() : P.step;
		st.last = now;
		bool fresh = !st.has || (int)st.p.size() != n;
		if (!fresh)
			for (int i = 0; i < n && !fresh; i++)
				fresh = !std::isfinite(st.p[i].x) || !std::isfinite(st.p[i].y) || !std::isfinite(st.p[i].z) ||   // NaN
				        std::fabs(st.p[i].x - rest[i].x) > P.reset || std::fabs(st.p[i].y - rest[i].y) > P.reset ||
				        std::fabs(st.p[i].z - rest[i].z) > P.reset;
		if (fresh) {
			st.p = rest;
			st.prev = rest;
			st.has = true;
		}
		std::vector<NiPoint3>& p = st.p;
		std::vector<NiPoint3>& prev = st.prev;
		int steps = (int)std::lround(dt / P.step);
		steps = steps < 1 ? 1 : (steps > 4 ? 4 : steps);
		const float lim = P.maxStep / scale;
		for (int s = 0; s < steps; s++) {
			for (int i = 0; i < n; i++) {
				const NiPoint3 v = (p[i] - prev[i]) * (1.0f - P.damping);
				const NiPoint3 old = p[i];
				NiPoint3 q = p[i] + v - up * (P.gravity / scale);
				const float k = P.stiffness[std::min((int)P.stiffness.size() - 1, i % nl)];
				q += (rest[i] - q) * k;
				NiPoint3 move = q - old;
				const float ml = Len(move);
				if (ml > lim)
					q = old + move * (lim / ml);
				prev[i] = old;
				p[i] = q;
			}
			for (int it = 0; it < P.iterations; it++) {
				// the top level stays near its rest (the waist holds the cloth)
				for (int c = 0; c < nc; c++) {
					const int i = c * nl;
					NiPoint3 d = p[i] - rest[i];
					const float dl = Len(d), h = P.hold / scale;
					if (dl > h)
						p[i] = rest[i] + d * (h / dl);
				}
				// columns keep their length both ways
				for (int c = 0; c < nc; c++)
					for (int l = 1; l < nl; l++) {
						const int i = c * nl + l, j = i - 1;
						NiPoint3 d = p[i] - p[j];
						const float dl = Len(d), hi = chain[i] * P.stretch, lo = chain[i] * P.squash;
						if (dl > hi)
							p[i] = p[j] + d * (hi / dl);
						else if (dl < lo) {
							NiPoint3 dir = dl > 1e-6f ? d / dl : (rest[i] - rest[j]) / (chain[i] > 1e-6f ? chain[i] : 1.0f);
							p[i] = p[j] + dir * lo;
						}
					}
				// neighbours do not tear apart
				for (int c = 0; c < nc; c++)
					for (int l = 0; l < nl; l++) {
						const int i = c * nl + l, j = ((c + 1) % nc) * nl + l;
						NiPoint3 d = p[j] - p[i];
						const float dl = Len(d), m = ring[i] * P.spread;
						if (dl > m) {
							const NiPoint3 corr = d * (0.5f * (dl - m) / dl);
							p[i] += corr;
							p[j] -= corr;
						}
					}
				// the legs (tools/skirt.py Solver._constrain)
				for (int ci = 0; ci < 4; ci++) {
					const NiPoint3 a = capA[ci];
					NiPoint3 u = capB[ci] - a;
					const float L = Len(u);
					if (L < 1e-4f)
						continue;
					u = u / L;
					auto side = [&](int i, const NiPoint3& d, float dn, NiPoint3& out) {
						NiPoint3 h = hint[i] - u * Dot(hint[i], u);
						const float hn = Len(h);
						if (hn > 0.1f) {
							out = h / hn;
							return true;
						}
						if (dn > 1e-6f) {
							out = d / dn;
							return true;
						}
						return false;
					};
					for (int i = 0; i < n; i++) {
						float t = Dot(p[i] - a, u);
						t = t < 0.0f ? 0.0f : (t > L ? L : t);
						const NiPoint3 cp = a + u * t;
						const NiPoint3 d = p[i] - cp;
						const float dn = Len(d), rad = clear[i][ci] / scale;
						if (dn >= rad)
							continue;
						NiPoint3 dir, hs;
						const bool hasSide = side(i, d, dn, hs);
						// touched: straight out, unless that is the far side from its column; landed on: its column's side
						if (dn > P.deep * rad && dn > 1e-6f && (!hasSide || Dot(d / dn, hs) > -P.away))
							dir = d / dn;
						else if (hasSide)
							dir = hs;
						else
							continue;
						p[i] = cp + dir * rad;
					}
					for (int c = 0; c < nc; c++)
						for (int l = 1; l < nl; l++) {
							const int i = c * nl + l, j = i - 1;
							const float segLim = clear[i][4 + ci] / scale;
							if (segLim <= 0.0f)
								continue;
							NiPoint3 q, cp;
							ClosestSegments(p[j], p[i], a, capB[ci], q, cp);
							if (Len(q - cp) >= P.cross * segLim)
								continue;   // only a segment the leg's axis really passes through
							float t = Dot(p[i] - a, u);
							t = t < 0.0f ? 0.0f : (t > L ? L : t);
							const NiPoint3 at = a + u * t;
							const NiPoint3 d = p[i] - at;
							const float rad = clear[i][ci] / scale;
							NiPoint3 dir;
							if (!side(i, d, Len(d), dir))
								continue;
							if (Dot(d, dir) >= rad)
								continue;   // already on that side and clear: the segment grazes
							p[i] = at + dir * rad;
						}
				}
			}
		}
		// a NaN (it fails every comparison above) or a point gone far: the whole skirt back to rest (Thing.cpp's guard)
		for (int i = 0; i < n; i++)
			if (!std::isfinite(p[i].x) || !std::isfinite(p[i].y) || !std::isfinite(p[i].z) ||
					std::fabs(p[i].x - rest[i].x) > P.reset || std::fabs(p[i].y - rest[i].y) > P.reset ||
					std::fabs(p[i].z - rest[i].z) > P.reset) {
				p = rest;
				prev = rest;
				char bad[64];
				_snprintf_s(bad, sizeof(bad), _TRUNCATE, "skirt|nan|%08X", actor->formID);
				Note(bad, "[skirt] %08X: a skirt point went non-finite or far off: back to rest\n", actor->formID);
				break;
			}
		// the nodes: local = the solved point, identity rotation; world at once, so this frame's skin sees it
		for (int i = 0; i < n; i++) {
			NiAVObject* node = nodes[i];
			if (!node)
				continue;
			NiTransform& l = G::Local(node);
			l.pos = p[i];
			for (int r = 0; r < 3; r++)
				for (int c = 0; c < 4; c++)
					l.rot.data[r][c] = r == c ? 1.0f : 0.0f;
			NiTransform& w = G::World(node);
			w.rot = l.rot * pw.rot;
			w.pos = pw.pos + pw.rot.Transpose() * (l.pos * pw.scale);
			w.scale = pw.scale * l.scale;
		}
		char key[64];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "skirt|actor|%08X|%d", actor->formID, found);
		Note(key, "[skirt] %08X: %d skirt node(s) moved (%s)\n", actor->formID, found, male ? "a man" : "a woman");
	}
}

void LoadSkirtConfig(INIReader& reader)
{
	P = Params();
	P.enabled = reader.GetBoolean("Skirt", "enabled", true);
	P.columns = (int)reader.GetInteger("Skirt", "columns", 12);
	P.levels = (int)reader.GetInteger("Skirt", "levels", 5);
	P.iterations = (int)reader.GetInteger("Skirt", "iterations", 3);
	P.prefix = reader.Get("Skirt", "prefix", "AnatSkirt_");
	P.stiffness = Floats(reader.Get("Skirt", "stiffness", "0.5,0.3,0.22,0.17,0.14"));
	if (P.stiffness.empty())
		P.stiffness.push_back(0.2f);
	auto f = [&](const char* k, float d) { return (float)reader.GetReal("Skirt", k, d); };
	P.damping = f("damping", 0.12f);
	P.hold = f("hold", 4.0f);
	P.stretch = f("stretch", 1.05f);
	P.squash = f("squash", 0.9f);
	P.spread = f("spread", 1.6f);
	P.hintUp = f("hintUp", 0.5f);
	P.reset = f("reset", 60.0f);
	P.step = f("step", 1.0f / 60.0f);
	P.maxStep = f("maxStep", 2.0f);
	P.gravity = f("gravity", 0.1f);
	P.deep = f("deep", 0.75f);
	P.cross = f("cross", 0.5f);
	P.away = f("away", 0.2f);
	clearF.clear();
	clearM.clear();
	const auto sections = reader.Sections();          // Section() throws on a section the file lacks
	if (sections.count("SkirtClear"))
		for (auto& e : reader.Section("SkirtClear"))
			clearF[LowerOf(e.first)] = Floats(e.second);
	if (sections.count("SkirtClearMale"))
		for (auto& e : reader.Section("SkirtClearMale"))
			clearM[LowerOf(e.first)] = Floats(e.second);
	if (P.columns < 3 || P.levels < 1 || P.columns * P.levels > 512)
		P.enabled = false;
	Note("skirt|config|" + std::to_string(clearF.size()) + "|" + std::to_string(clearM.size()),
		"[skirt] %s: %d columns x %d levels, clearances for %d (women) and %d (men) nodes\n",
		P.enabled ? "on" : "off", P.columns, P.levels, (int)clearF.size(), (int)clearM.size());
	ResetSkirt();
}

void UpdateSkirt(Actor* actor)
{
	if (!P.enabled || clearF.empty() || !actor)
		return;
	__try {
		UpdateImpl(actor);
	} __except (1) {
		static bool said = false;
		if (!said) {
			said = true;
			spdlog::info("[skirt] a fault while an actor's 3D changed was caught; that actor is skipped this frame");
		}
	}
}

void ResetSkirt()
{
	std::lock_guard<std::mutex> guard(lock);
	states.clear();
}
