// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-23: the contact-driven mouth, written over the face's merged morphs (ocbp.ini [Mouth]).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Mouth.h"

#include "ActorEntry.h"
#include "ActorUtils.h"
#include "CollisionHub.h"
#include "config.h"
#include "log.h"

#include "f4se/GameReferences.h"
#include "f4se/NiNodes.h"
#include "f4se/NiObjects.h"
#include "f4se_common/BranchTrampoline.h"
#include "f4se_common/Relocation.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	// ---- ocbp.ini [Mouth]; fo4-anatomy's tools/physics_config.py writes it, tools/mouth.py measures it
	bool enabled = false;
	std::vector<std::vector<std::string>> chainNames;   // penis bones, base to tip, one chain per list
	bool useProps = false;           // hand props at the mouth: off, eating/drinking idles use the same nodes
	NiPoint3 femaleMouth(-1.80f, 8.12f, 0.0f);   // where the lips meet, in the HEAD bone's space
	NiPoint3 maleMouth(-1.84f, 7.78f, 0.0f);
	NiPoint3 facingBone(0.065f, 0.998f, 0.0f);   // out of the mouth, HEAD bone space
	NiPoint3 upBone(0.998f, -0.065f, 0.0f);      // the face's up, HEAD bone space
	float femaleGap = 2.97f;         // how far apart Jaw Open 1.0 puts the lips (the base heads' .tri)
	float maleGap = 2.30f;
	float halfWidth = 2.9f;          // a crossing further to the side is not in the mouth
	float below = 3.0f;              // ... further below the lip line (the chin)
	float above = 1.5f;              // ... further above it (the nose)
	float skin = 0.45f;              // collider radius minus this = the visible radius
	float ahead = 3.0f;              // a tip this close in front of the lips starts opening them
	float anticipate = 0.3f;         // Jaw Open when a tip touches the lips from outside
	float margin = 0.08f;            // Jaw Open beyond the exact fit, so the lips clear the shaft
	float funnelMax = 0.3f;          // both lip funnels while something is inside: the lips round on it
	float liftMove = 0.53f;          // Upper Lip Up's full move: lifts the lip off a shaft riding high
	float openRate = 20.0f;          // 1/s toward a wider mouth
	float closeRate = 6.0f;          // 1/s toward a narrower one
	float blendRate = 12.0f;         // 1/s: how fast the override takes over and gives the mouth back
	float holdSeconds = 0.35f;       // keeps the shape between two strokes

	// ---- the engine, 1.10.163 (Steam and GOG share the code: checked byte for byte) ----
	RelocAddr<uintptr_t> mergeFn(0x6689D0);
	RelocAddr<uintptr_t> mergeCall(0x6860FA);
	RelocAddr<uintptr_t> faceDataVtable(0x2CE9C58);
	const unsigned char kMergePrologue[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x78, 0x20,
	                                         0x41, 0x56, 0x48, 0x83, 0xEC, 0x60 };
	const int kWeights = 0x18;                  // float[54]: what the face mesh is built from
	const int kJawOpen = 2;
	const int kLeftUpperLipUp = 21;
	const int kLowerLipFunnel = 22;
	const int kRightUpperLipUp = 44;
	const int kUpperLipFunnel = 46;
	bool hooked = false;

	struct Override { void* data; float inside, jaw, floor, funnel, lift; };
	std::mutex publishLock;   // the face update may run on another thread than UpdateActors
	std::vector<Override> published;

	struct State
	{
		float inside = 0.0f, jaw = 0.0f, floor = 0.0f, funnel = 0.0f, lift = 0.0f;
		float sinceContact = 1e9f;
		unsigned frame = 0;
	};
	std::unordered_map<UInt32, State> states;
	unsigned frameCount = 0;
	LARGE_INTEGER lastTick = {};

	typedef bool (*MergeFn)(void* data, float dt, bool flag);
	MergeFn origMerge = nullptr;             // the engine's merge itself: its bytes are never touched

	inline float Dot(const NiPoint3& a, const NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline NiPoint3 Cross(const NiPoint3& a, const NiPoint3& b)
	{
		return NiPoint3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
	}
	inline float Length(const NiPoint3& a) { return std::sqrt(Dot(a, a)); }
	inline NiPoint3 Normalized(const NiPoint3& a)
	{
		float l = Length(a);
		return l > 1e-6f ? a / l : NiPoint3(0.0f, 0.0f, 0.0f);
	}
	inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	inline float Toward(float cur, float target, float rate, float dt)
	{
		return cur + (target - cur) * (1.0f - std::exp(-rate * dt));
	}
	inline float Visible(float colliderRadius) { return (std::max)(0.2f, colliderRadius - skin); }

	std::vector<std::string> Split(const std::string& text, char sep)
	{
		std::vector<std::string> out;
		std::stringstream in(text);
		std::string part;
		while (std::getline(in, part, sep)) {
			part.erase(0, part.find_first_not_of(" \t"));
			part.erase(part.find_last_not_of(" \t") + 1);
			if (!part.empty())
				out.push_back(part);
		}
		return out;
	}

	void Note(const std::string& key, const char* fmt, ...)
	{
		char line[512];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
		va_end(args);
		AnatomyLogLine(key, line);
	}

	// The actor's face data: MiddleProcess data + 0x3C8, only if it is what the engine says it is.
	void* FaceData(Actor* actor)
	{
		auto process = actor->middleProcess;
		if (!process || !process->unk08)
			return nullptr;
		void* data = reinterpret_cast<void*>(process->unk08->unk3B0[3]);
		if (!data || *reinterpret_cast<uintptr_t*>(data) != faceDataVtable.GetUIntPtr())
			return nullptr;
		return data;
	}

	bool IsProp(const std::string& node)
	{
		return std::find(propNodes.begin(), propNodes.end(), node) != propNodes.end();
	}

	// Half the extent, along U, of a shaft's cross-section in the lip plane. A cylinder of radius r
	// along a, cut by the plane with normal F, is an ellipse: r / |a.F| along a's in-plane direction
	// m, and r across it (n).
	float SectionHalfU(const NiPoint3& a, const NiPoint3& F, const NiPoint3& U, float r)
	{
		float c = std::fabs(Dot(a, F));
		if (c < 0.25f)
			c = 0.25f;
		NiPoint3 m = a - F * Dot(a, F);
		float ml = Length(m);
		if (ml < 1e-4f)
			return r;
		m = m / ml;
		NiPoint3 n = Cross(F, m);
		float mu = Dot(m, U) * r / c;
		float nu = Dot(n, U) * r;
		return std::sqrt(mu * mu + nu * nu);
	}

	bool HookMerge(void* data, float dt, bool flag)
	{
		bool changed = origMerge(data, dt, flag);
		Override o;
		bool found = false;
		{
			std::lock_guard<std::mutex> guard(publishLock);
			for (auto& p : published) {
				if (p.data == data) {
					o = p;
					found = true;
					break;
				}
			}
		}
		if (!found)
			return changed;
		float* w = reinterpret_cast<float*>(reinterpret_cast<char*>(data) + kWeights);
		float jaw = w[kJawOpen] + (o.jaw - w[kJawOpen]) * o.inside;
		w[kJawOpen] = (std::max)(jaw, o.floor);
		w[kLowerLipFunnel] += (o.funnel - w[kLowerLipFunnel]) * o.inside;
		w[kUpperLipFunnel] += (o.funnel - w[kUpperLipFunnel]) * o.inside;
		w[kLeftUpperLipUp] += (o.lift - w[kLeftUpperLipUp]) * o.inside;
		w[kRightUpperLipUp] += (o.lift - w[kRightUpperLipUp]) * o.inside;
		return true;                              // the mesh is rebuilt from what we wrote
	}

	struct Point { NiPoint3 pos; float r; };
	struct Chain { Actor* owner; bool prop; std::string name; std::vector<Point> pts; };

	void Publish(std::vector<Override>&& next)
	{
		std::lock_guard<std::mutex> guard(publishLock);
		published.swap(next);
	}
}

void LoadMouthConfig(INIReader& reader)
{
	enabled = reader.GetBoolean("Mouth", "enabled", false);
	chainNames.clear();
	for (auto& chain : Split(reader.Get("Mouth", "chains", ""), '/')) {   // not ';': a comment after a space
		auto names = Split(chain, '|');
		if (!names.empty())
			chainNames.push_back(names);
	}
	useProps = reader.GetBoolean("Mouth", "props", false);
	auto point = [&](const char* prefix, NiPoint3 fallback) {
		std::string p(prefix);
		return NiPoint3((float)reader.GetReal("Mouth", p + "X", fallback.x),
		                (float)reader.GetReal("Mouth", p + "Y", fallback.y),
		                (float)reader.GetReal("Mouth", p + "Z", fallback.z));
	};
	femaleMouth = point("female", femaleMouth);
	maleMouth = point("male", maleMouth);
	facingBone = Normalized(point("facing", facingBone));
	upBone = Normalized(point("up", upBone));
	femaleGap = (std::max)(0.5f, (float)reader.GetReal("Mouth", "femaleGap", femaleGap));
	maleGap = (std::max)(0.5f, (float)reader.GetReal("Mouth", "maleGap", maleGap));
	halfWidth = (float)reader.GetReal("Mouth", "halfWidth", halfWidth);
	below = (float)reader.GetReal("Mouth", "below", below);
	above = (float)reader.GetReal("Mouth", "above", above);
	skin = (float)reader.GetReal("Mouth", "skin", skin);
	ahead = (std::max)(0.1f, (float)reader.GetReal("Mouth", "ahead", ahead));
	anticipate = Clamp((float)reader.GetReal("Mouth", "anticipate", anticipate), 0.0f, 1.0f);
	margin = (float)reader.GetReal("Mouth", "margin", margin);
	funnelMax = Clamp((float)reader.GetReal("Mouth", "funnel", funnelMax), 0.0f, 1.0f);
	liftMove = (std::max)(0.05f, (float)reader.GetReal("Mouth", "liftMove", liftMove));
	openRate = (float)reader.GetReal("Mouth", "openRate", openRate);
	closeRate = (float)reader.GetReal("Mouth", "closeRate", closeRate);
	blendRate = (float)reader.GetReal("Mouth", "blendRate", blendRate);
	holdSeconds = (float)reader.GetReal("Mouth", "holdSeconds", holdSeconds);
	if (enabled && chainNames.empty() && !useProps)
		enabled = false;                          // nothing could ever reach a mouth
}

void InstallMouthHook()
{
	if (hooked)
		return;
	if (!enabled) {
		Note("mouth|off", "[mouth] off (ocbp.ini [Mouth] enabled=0 or no chains)\n");
		return;
	}
	// never patch a build we have not read: the merge's prologue, the one call to it, and the vtable
	const unsigned char* code = reinterpret_cast<const unsigned char*>(mergeFn.GetUIntPtr());
	const unsigned char* call = reinterpret_cast<const unsigned char*>(mergeCall.GetUIntPtr());
	bool prologue = std::memcmp(code, kMergePrologue, sizeof(kMergePrologue)) == 0;
	bool calls = call[0] == 0xE8 &&
		mergeCall.GetUIntPtr() + 5 + *reinterpret_cast<const int32_t*>(call + 1) == mergeFn.GetUIntPtr();
	if (!prologue || !calls) {
		Note("mouth|build", "[mouth] this game build is not the one the mouth was read from (prologue %d, "
			"call %d): the mouth stays off\n", (int)prologue, (int)calls);
		return;
	}
	// The engine's one CALL to the merge is pointed at ours, and ours calls the merge as it is. Until
	// 2026-09-23 the merge's entry was detoured instead, with DetourXS, which measures the prologue with
	// LDE in 32-bit mode: it took 14 bytes where the whole instructions are 17, and resumed the merge on
	// the tail of "sub rsp,60h", a "sub esp,60h" that clears the stack pointer's upper half. Every save
	// load then died at +0x668A0D, the merge's first push, with no crash log (no stack to report on).
	if (!g_branchTrampoline.Create(1024 * 64)) {
		Note("mouth|hook", "[mouth] no room for a branch trampoline near the game: the mouth stays off\n");
		return;
	}
	origMerge = reinterpret_cast<MergeFn>(mergeFn.GetUIntPtr());
	if (!g_branchTrampoline.Write5Call(mergeCall.GetUIntPtr(), reinterpret_cast<uintptr_t>(&HookMerge))) {
		Note("mouth|hook", "[mouth] could not point the merge's call at the mouth: the mouth stays off\n");
		return;
	}
	// read the patch back: the call must now reach a stub that jumps to HookMerge
	uintptr_t stub = mergeCall.GetUIntPtr() + 5 + *reinterpret_cast<const int32_t*>(call + 1);
	const unsigned char* s = reinterpret_cast<const unsigned char*>(stub);
	bool reaches = call[0] == 0xE8 && s[0] == 0xFF && s[1] == 0x25 && *reinterpret_cast<const uint32_t*>(s + 2) == 0 &&
		*reinterpret_cast<const uintptr_t*>(s + 6) == reinterpret_cast<uintptr_t>(&HookMerge);
	hooked = reaches;
	Note("mouth|on", "[mouth] %s: %d chain(s), props %d, gap F %.2f M %.2f; the merge's call hooked, its code "
		"untouched (prologue still the engine's: %d)\n", reaches ? "on" : "OFF, the hooked call does not reach it",
		(int)chainNames.size(), (int)useProps, femaleGap, maleGap,
		(int)(std::memcmp(code, kMergePrologue, sizeof(kMergePrologue)) == 0));
}

void UpdateMouths()
{
	if (!hooked || !enabled) {
		if (!published.empty())
			Publish({});
		return;
	}
	LARGE_INTEGER now, freq;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&freq);
	float dt = lastTick.QuadPart ? (float)(now.QuadPart - lastTick.QuadPart) / (float)freq.QuadPart : 0.0f;
	lastTick = now;
	dt = Clamp(dt, 0.0f, 0.1f);
	frameCount++;

	// what could be in a mouth: every penis chain, base to tip, and (if on) every hand prop
	std::vector<Chain> chains;
	std::unordered_map<Actor*, std::unordered_map<std::string, const Collision*>> byActor;
	for (auto& c : otherColliders) {
		if (!c.colliderActor || c.collisionSpheres.empty())
			continue;
		if (IsProp(c.colliderNodeName)) {
			if (!useProps)
				continue;
			Chain ch{ c.colliderActor, true, c.colliderNodeName, {} };
			for (auto& s : c.collisionSpheres)
				ch.pts.push_back(Point{ s.worldPos, (float)s.radius });
			chains.push_back(ch);
			continue;
		}
		byActor[c.colliderActor][c.colliderNodeName] = &c;
	}
	for (auto& actorNodes : byActor) {
		for (auto& names : chainNames) {
			Chain ch{ actorNodes.first, false, names.front(), {} };
			for (auto& n : names) {
				auto it = actorNodes.second.find(n);
				if (it != actorNodes.second.end())
					for (auto& s : it->second->collisionSpheres)
						ch.pts.push_back(Point{ s.worldPos, (float)s.radius });
			}
			if (!ch.pts.empty())
				chains.push_back(ch);
		}
	}

	std::vector<Override> next;
	BSFixedString headName("HEAD");
	for (auto& e : actorEntries) {
		Actor* a = e.actor;
		if (!a || !a->unkF0 || !a->unkF0->rootNode)
			continue;
		void* data = FaceData(a);
		if (!data)
			continue;
		NiAVObject* head = a->unkF0->rootNode->GetObjectByName(&headName);
		if (!head)
			continue;
		bool male = actorUtils::IsActorMale(a);
		const NiTransform& t = head->m_worldTransform;
		// a node's local offset reaches the world through the TRANSPOSE of its stored rotation (the
		// fork's own Thing.cpp: parent pos + parent rot^T * local, proven by OCBPC's physics)
		NiMatrix43 toWorld = t.rot.Transpose();
		NiPoint3 M = t.pos + toWorld * ((male ? maleMouth : femaleMouth) * t.scale);
		NiPoint3 F = Normalized(toWorld * facingBone);
		NiPoint3 U = Normalized(toWorld * upBone);
		NiPoint3 S = Cross(F, U);

		char key[64];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "mouth|%08X", a->formID);
		Note(key, "[mouth] %08X (%s): HEAD (%.1f, %.1f, %.1f) scale %.2f -> mouth (%.1f, %.1f, %.1f) facing "
			"(%.2f, %.2f, %.2f)\n", a->formID, male ? "male" : "female", t.pos.x, t.pos.y, t.pos.z, t.scale,
			M.x, M.y, M.z, F.x, F.y, F.z);

		float need = 0.0f, over = -1e9f, approach = 0.0f;
		bool inside = false;
		const Chain* who = nullptr;
		for (auto& ch : chains) {
			if (ch.owner == a && !ch.prop)
				continue;                         // her own penis bones are never in her mouth
			if (Length(ch.pts.back().pos - M) > 60.0f && Length(ch.pts.front().pos - M) > 60.0f)
				continue;
			auto consider = [&](const NiPoint3& X, float r, const NiPoint3& dir) {
				NiPoint3 q = X - M;
				float qs = Dot(q, S), qu = Dot(q, U);
				if (std::fabs(qs) > halfWidth || qu < -below || qu > above)
					return;
				float hu = SectionHalfU(dir, F, U, r);
				inside = true;
				who = &ch;
				need = (std::max)(need, hu - qu);     // the lower lip drops below its bottom
				over = (std::max)(over, qu + hu);     // its top above the lip line lifts the upper lip
			};
			for (size_t k = 0; k + 1 < ch.pts.size(); k++) {
				const Point& p0 = ch.pts[k];
				const Point& p1 = ch.pts[k + 1];
				float d0 = Dot(p0.pos - M, F), d1 = Dot(p1.pos - M, F);
				if ((d0 > 0.0f) == (d1 > 0.0f))
					continue;
				float s = d0 / (d0 - d1);
				consider(p0.pos + (p1.pos - p0.pos) * s, Visible(p0.r + (p1.r - p0.r) * s),
					Normalized(p1.pos - p0.pos));
			}
			const Point& base = ch.pts.front();
			float dB = Dot(base.pos - M, F);
			if (dB <= 0.0f && dB > -(Visible(base.r) + 2.0f) && ch.pts.size() > 1)   // all of it inside
				consider(base.pos - F * dB, Visible(base.r), Normalized(ch.pts[1].pos - base.pos));
			const Point& tip = ch.pts.back();
			float dT = Dot(tip.pos - M, F), rT = Visible(tip.r);
			if (std::fabs(dT) < rT) {
				NiPoint3 dir = ch.pts.size() > 1 ? Normalized(tip.pos - ch.pts[ch.pts.size() - 2].pos) : F * -1.0f;
				consider(tip.pos - F * dT, std::sqrt(rT * rT - dT * dT), dir);
			}
			else if (dT > 0.0f && dT - rT < ahead) {
				NiPoint3 q = tip.pos - M - F * dT;
				if (std::fabs(Dot(q, S)) <= halfWidth && Dot(q, U) >= -below && Dot(q, U) <= above)
					approach = (std::max)(approach, 1.0f - (dT - rT) / ahead);
			}
		}

		State& st = states[a->formID];
		st.frame = frameCount;
		float gap = male ? maleGap : femaleGap;
		if (inside) {
			float jawT = Clamp(need / gap + margin, 0.0f, 1.0f);
			float liftT = over > 0.0f ? Clamp(over / liftMove, 0.0f, 1.0f) : 0.0f;
			st.jaw = Toward(st.jaw, jawT, jawT > st.jaw ? openRate : closeRate, dt);
			st.funnel = Toward(st.funnel, funnelMax, openRate, dt);
			st.lift = Toward(st.lift, liftT, liftT > st.lift ? openRate : closeRate, dt);
			st.sinceContact = 0.0f;
			char pair[96];
			_snprintf_s(pair, sizeof(pair), _TRUNCATE, "mouth|%08X|%08X|%s", a->formID,
				who && who->owner ? who->owner->formID : 0, who ? who->name.c_str() : "");
			Note(pair, "[mouth] %08X: %s of %08X at her lips: lower lip %.2f down -> Jaw Open %.2f, upper lip "
				"%.2f up\n", a->formID, who ? who->name.c_str() : "?", who && who->owner ? who->owner->formID : 0,
				need, jawT, over > 0.0f ? over : 0.0f);
		}
		else {
			st.sinceContact += dt;               // the shape holds; the blend below gives it back
		}
		st.floor = Toward(st.floor, anticipate * approach, anticipate * approach > st.floor ? openRate : closeRate, dt);
		st.inside = Toward(st.inside, st.sinceContact <= holdSeconds ? 1.0f : 0.0f, blendRate, dt);
		if (st.inside > 0.001f || st.floor > 0.001f)
			next.push_back(Override{ data, st.inside, st.jaw, st.floor, st.funnel, st.lift });
	}
	// forget whoever left: a stale entry would steer a face that is not theirs any more
	for (auto it = states.begin(); it != states.end();)
		it = it->second.frame == frameCount ? std::next(it) : states.erase(it);
	Publish(std::move(next));
}
