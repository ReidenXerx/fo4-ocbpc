// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-07: the vaginal canal wraps the shaft inside her (ocbp.ini [Canal]).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Canal.h"

#include "ActorUtils.h"
#include "Aim.h"
#include "Bones.h"
#include "Game.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	struct Params
	{
		bool enabled = true;
		int rings = 5, spokes = 8;
		std::string prefix = "AnatCanal_";
		float radius = 1.68f, lead = 1.5f, rate = 10.0f, reach = 5.0f;
	} P;

	struct State
	{
		std::vector<NiPoint3> p;                     // each node's local position as last written
		std::chrono::steady_clock::time_point last;
		bool moved = false;                          // something written off rest: put back before forgetting her
	};
	std::unordered_map<UInt32, State> states;
	std::mutex lock;

	float Dot(const NiPoint3& a, const NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	float Len(const NiPoint3& a) { return std::sqrt(Dot(a, a)); }
	NiPoint3 Unit(const NiPoint3& a)
	{
		float l = Len(a);
		return l > 1e-6f ? a / l : NiPoint3(0.0f, 0.0f, 1.0f);
	}

	std::string Name(int ring, int spoke)
	{
		char buf[64];
		_snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s%d_%d", P.prefix.c_str(), ring + 1, spoke);
		return buf;
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

	// the point of the polyline nearest p
	NiPoint3 Nearest(const std::vector<NiPoint3>& line, const NiPoint3& p)
	{
		NiPoint3 best = line.front();
		float bestD = Len(p - best);
		for (size_t i = 0; i + 1 < line.size(); i++) {
			const NiPoint3 a = line[i], ab = line[i + 1] - line[i];
			const float len2 = Dot(ab, ab);
			float t = len2 > 1e-9f ? Dot(p - a, ab) / len2 : 0.0f;
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			const NiPoint3 q = a + ab * t;
			const float d = Len(p - q);
			if (d < bestD) {
				bestD = d;
				best = q;
			}
		}
		return best;
	}

	bool Decide(UInt32 id, const std::vector<NiPoint3>& rest, const NiTransform& pw, std::vector<NiPoint3>& out);

	void UpdateImpl(Actor* actor)
	{
		NiAVObject* root = G::Root(actor);
		if (!root || actorUtils::IsActorMale(actor))
			return;
		BSFixedString pelvisName("Pelvis_skin");
		NiAVObject* pelvis = root->GetObjectByName(pelvisName);
		if (!pelvis)
			return;
		const int R = P.rings, S = P.spokes, n = R * S;
		std::vector<NiAVObject*> nodes(n, nullptr);
		std::vector<NiPoint3> rest(n);
		for (int i = 0; i < n; i++) {
			const std::string nm = Name(i / S, i % S);
			BSFixedString want(nm.c_str());
			NiAVObject* node = pelvis->GetObjectByName(want);
			if (!node || G::Parent(node) != pelvis)
				return;                                  // a body without our rings (or an older one): nothing to move
			if (!AnatomyBoneRest(nm, false, rest[i])) {
				Note("canal|rest|" + nm, "[canal] %s has no [Bones] rest: the wrap is off\n", nm.c_str());
				return;
			}
			nodes[i] = node;
		}

		const NiTransform& pw = G::World(pelvis);
		std::vector<NiPoint3> out;
		if (!Decide(actor->formID, rest, pw, out))
			return;
		// local = the point, identity rotation; world at once, so this frame's skin sees it. Outside the lock: a fault
		// here (her 3D changing under us) must not leave it held
		for (int i = 0; i < n; i++) {
			NiTransform& l = G::Local(nodes[i]);
			l.pos = out[i];
			for (int r = 0; r < 3; r++)
				for (int c = 0; c < 4; c++)
					l.rot.data[r][c] = r == c ? 1.0f : 0.0f;
			NiTransform& w = G::World(nodes[i]);
			w.rot = l.rot * pw.rot;
			w.pos = pw.pos + pw.rot.Transpose() * (l.pos * pw.scale);
			w.scale = pw.scale * l.scale;
		}
	}

	// where her ring nodes go this frame (Pelvis_skin's frame); false: leave them alone
	bool Decide(UInt32 id, const std::vector<NiPoint3>& rest, const NiTransform& pw, std::vector<NiPoint3>& out)
	{
		const int R = P.rings, S = P.spokes, n = R * S;
		std::lock_guard<std::mutex> guard(lock);
		State& st = states[id];
		const auto now = std::chrono::steady_clock::now();
		float dt = st.p.empty() ? 0.0f : std::chrono::duration<float>(now - st.last).count();
		dt = dt < 0.0f ? 0.0f : (dt > 0.1f ? 0.1f : dt);
		st.last = now;
		if (st.p.size() != (size_t)n)
			st.p = rest;

		// the shaft in her vagina this frame, in Pelvis_skin's frame (local = R (world - pos) / scale)
		const float scale = pw.scale > 1e-6f ? pw.scale : 1.0f;
		std::vector<NiPoint3> shaft;
		std::vector<NiPoint3> world;
		if (P.enabled && (AimReceivedKinds(id) & 1u) && AimShaft(id, 0, world) && world.size() >= 2)
			for (auto& w : world)
				shaft.push_back((pw.rot * (w - pw.pos)) / scale);
		if (shaft.empty() && !st.moved) {
			states.erase(id);
			return false;                                // at rest and nothing inside: leave the nodes alone
		}

		// each ring: its centre and spokes' directions from where [Bones] rests them, its axis toward the next ring
		std::vector<NiPoint3> centre(R);
		for (int k = 0; k < R; k++) {
			NiPoint3 c(0.0f, 0.0f, 0.0f);
			for (int j = 0; j < S; j++)
				c += rest[k * S + j];
			centre[k] = c / (float)S;
		}
		std::vector<NiPoint3> want = rest;
		if (!shaft.empty()) {
			const NiPoint3 tip = shaft.back();
			for (int k = 0; k < R; k++) {
				const NiPoint3 axis = Unit(centre[(std::min)(R - 1, k + 1)] - centre[(std::max)(0, k - 1)]);
				// the tip's travel past this ring (negative: still short of it); open fully as it arrives
				float open = (Dot(tip - centre[k], axis) + P.lead) / P.lead;
				open = open < 0.0f ? 0.0f : (open > 1.0f ? 1.0f : open);
				open = open * open * (3.0f - 2.0f * open);
				const NiPoint3 at = Nearest(shaft, centre[k]);
				if (open <= 0.0f || Len(at - centre[k]) > P.reach)
					continue;
				for (int j = 0; j < S; j++) {
					const int i = k * S + j;
					const NiPoint3 dir = Unit(rest[i] - centre[k]);
					const float r = (std::max)(P.radius, Len(rest[i] - centre[k]));
					want[i] = rest[i] + ((at + dir * r) - rest[i]) * open;
				}
			}
		}
		const float a = dt > 0.0f ? 1.0f - std::exp(-P.rate * dt) : 1.0f;
		bool off = false;
		for (int i = 0; i < n; i++) {
			st.p[i] = st.p[i] + (want[i] - st.p[i]) * a;
			if (Len(st.p[i] - rest[i]) > 1e-3f)
				off = true;
			else if (shaft.empty())
				st.p[i] = rest[i];
		}
		if (!st.moved && off)
			Note("canal|first", "[canal] %08X: the canal wraps a shaft (%d rings x %d spokes, radius %.2f)\n", id, R, S, P.radius);
		st.moved = off;
		out = st.p;
		if (!off && shaft.empty())
			states.erase(id);
		return true;
	}
}

void LoadCanalConfig(INIReader& reader)
{
	P = Params();
	P.enabled = reader.GetBoolean("Canal", "enabled", true);
	P.rings = (int)reader.GetInteger("Canal", "rings", 5);
	P.spokes = (int)reader.GetInteger("Canal", "spokes", 8);
	P.prefix = reader.Get("Canal", "prefix", "AnatCanal_");
	auto f = [&](const char* k, float d) { return (float)reader.GetReal("Canal", k, d); };
	P.radius = f("radius", 1.68f);
	P.lead = (std::max)(0.1f, f("lead", 1.5f));
	P.rate = f("rate", 10.0f);
	P.reach = f("reach", 5.0f);
	if (P.rings < 1 || P.spokes < 3 || P.rings * P.spokes > 128)
		P.enabled = false;
}

void UpdateCanal(Actor* actor)
{
	if (!actor)
		return;
	__try {
		UpdateImpl(actor);
	} __except (1) {
		static bool said = false;
		if (!said) {
			said = true;
			spdlog::info("[canal] a fault while an actor's 3D changed was caught; that actor is skipped this frame");
		}
	}
}

void ResetCanal()
{
	std::lock_guard<std::mutex> guard(lock);
	states.clear();
}
