// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-25: glances, one actor looking into another's eyes (RFAG).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Eyes.h"

#include "ActorEntry.h"
#include "ActorUtils.h"
#include "CollisionHub.h"
#include "FaceAuthority.h"
#include "Glance.h"
#include "Mouth.h"

#include "f4se/BSGeometry.h"
#include "f4se/GameForms.h"
#include "f4se/GameObjects.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"
#include "f4se/NiMaterials.h"
#include "f4se/NiNodes.h"
#include "f4se/NiObjects.h"
#include "f4se/NiProperties.h"
#include "f4se_common/BranchTrampoline.h"
#include "f4se_common/Relocation.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
	bool enabled = true;
	bool glancesOn = false;
	bool probe = false;
	bool test = false;
	float eyeRise = 4.8f, eyeBack = 0.8f;
	GlanceMath::Params params;
	const float kLidRate = 10.0f;               // 1/s: the lids open for a glance and give the face back
	const float kProbeEvery = 0.5f;             // s between probe lines per actor
	const int kProbeLines = 40;                 // per actor

	// ---- the engine, 1.10.163 (GOG; SAM's offsets, byte for byte) ----
	RelocAddr<uintptr_t> eyeFn(0x9C0410);
	RelocAddr<uintptr_t> eyeCalls[2] = { RelocAddr<uintptr_t>(0xD38B13), RelocAddr<uintptr_t>(0xD39681) };
	const unsigned char kEyePrologue[] = { 0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8D, 0xA8, 0xE8, 0xF8, 0xFF, 0xFF,
	                                       0x48, 0x81, 0xEC, 0x10, 0x08, 0x00, 0x00 };
	typedef void (*EyeFn)(float dt);
	EyeFn origEyes = nullptr;
	bool hooked = false;

	struct Look
	{
		float weight = 0.0f;                    // 0..1: how far the lids are held open
		float lidsOpen = 0.0f;
		bool active = false;
	};
	std::mutex lookLock;                        // the eye update's thread writes, UpdateMouths reads
	std::unordered_map<UInt32, Look> looks;

	// the probe's and the test's own clocks, per actor (the scan's thread only)
	struct Watch
	{
		float since = 1e9f;
		int lines = 0;
	};
	std::unordered_map<UInt32, Watch> watched;
	float testClock = 0.0f;

	void Note(const std::string& key, const char* fmt, ...)
	{
		if (AnatomyLogSeen(key))
			return;
		char line[512];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
		va_end(args);
		AnatomyLogLine(key, line);
	}

	inline float Dot(const NiPoint3& a, const NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline NiPoint3 Cross(const NiPoint3& a, const NiPoint3& b)
	{
		return NiPoint3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
	}
	inline float Length(const NiPoint3& a) { return std::sqrt(Dot(a, a)); }

	Actor* ActorOf(UInt32 formID)
	{
		TESForm* form = formID ? LookupFormByID(formID) : nullptr;
		return form ? DYNAMIC_CAST(form, TESForm, Actor) : nullptr;
	}

	// Where an actor's eyes are, and its head's frame: F out of the face, U up it, S = F x U
	bool EyesOf(Actor* a, NiPoint3& at, NiPoint3& F, NiPoint3& U, NiPoint3& S)
	{
		NiPoint3 mouth;
		if (!a || !MouthOpening(a, mouth, F, &U))
			return false;
		S = Cross(F, U);
		at = mouth + U * eyeRise - F * eyeBack;
		return true;
	}

	// The unit direction from a's eyes to b's, in a's head frame
	bool Toward(Actor* a, Actor* b, float& side, float& up, float& ahead)
	{
		NiPoint3 pa, Fa, Ua, Sa, pb, Fb, Ub, Sb;
		if (!EyesOf(a, pa, Fa, Ua, Sa) || !EyesOf(b, pb, Fb, Ub, Sb))
			return false;
		NiPoint3 d = pb - pa;
		float l = Length(d);
		if (l < 1.0f)
			return false;
		d = d / l;
		side = Dot(d, Sa);
		up = Dot(d, Ua);
		ahead = Dot(d, Fa);
		return true;
	}

	BSGeometry* FindEyes(NiAVObject* o, int depth)
	{
		if (!o || depth > 4)
			return nullptr;
		if (BSGeometry* g = o->GetAsBSGeometry()) {
			const char* n = o->m_name.c_str();
			std::string name = n ? n : "";
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return name.find("eyes") != std::string::npos ? g : nullptr;
		}
		NiNode* node = o->GetAsNiNode();
		if (!node)
			return nullptr;
		for (UInt16 i = 0; i < node->m_children.m_emptyRunStart; i++)
			if (BSGeometry* g = FindEyes(node->m_children.m_data[i], depth + 1))
				return g;
		return nullptr;
	}

	// The eyes' shader property: by the NPC's eyes head part, as SAM finds it, else any geometry named
	// "...Eyes..." near the root (a templated NPC's own head parts may be empty)
	BSShaderProperty* EyeProperty(Actor* a)
	{
		if (!a || !a->unkF0 || !a->unkF0->rootNode)
			return nullptr;
		NiNode* root = a->unkF0->rootNode;
		BSGeometry* geometry = nullptr;
		TESNPC* npc = DYNAMIC_CAST(a->baseForm, TESForm, TESNPC);
		for (UInt32 i = 0; npc && npc->headParts && i < npc->numHeadParts && !geometry; i++) {
			BGSHeadPart* part = npc->headParts[i];      // TESNPC::GetHeadPartByType, which this build does not link
			if (!part || part->type != BGSHeadPart::kTypeEyes)
				continue;
			BSFixedString name = part->partName;
			if (NiAVObject* o = root->GetObjectByName(&name))
				geometry = o->GetAsBSGeometry();
		}
		if (!geometry)
			geometry = FindEyes(root, 0);
		if (!geometry)
			return nullptr;
		NiProperty* p = geometry->shaderProperty;
		return p ? reinterpret_cast<BSShaderProperty*>(p) : nullptr;
	}

	void ApplyGlances(float dt)
	{
		auto running = FaceAuthority::Glances(EyeClockMs());
		std::unordered_set<UInt32> now;
		for (auto& r : running) {
			Actor* a = ActorOf(r.looker);
			Actor* b = ActorOf(r.glance.target);
			float side = 0.0f, up = 0.0f, ahead = 0.0f;
			GlanceMath::UV want;
			bool reach = a && b && Toward(a, b, side, up, ahead) && GlanceMath::Want(side, up, ahead, params, want);
			BSShaderProperty* property = reach && enabled && hooked ? EyeProperty(a) : nullptr;
			if (property && property->shaderMaterial) {
				GlanceMath::UV cur;
				property->shaderMaterial->GetOffsetUV(&cur.x, &cur.y);
				GlanceMath::UV next = GlanceMath::Step(cur, want, params, dt);
				property->iLastRenderPassState = 0x7FFFFFFF;   // the eye's UV is read again (SAM)
				property->shaderMaterial->SetOffsetUV(next.x, next.y);
			}
			char key[80];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "eyes|glance|%08X|%08X|%d", r.looker, r.glance.target,
				(int)reach + (int)(property != nullptr));
			Note(key, reach ? (property ? "[eyes] %08X looks into %08X's eyes: side %.2f, up %.2f, ahead %.2f -> uv "
				"(%.3f, %.3f), lids %.2f open\n" : "[eyes] %08X would look into %08X's eyes (side %.2f, up %.2f, ahead "
				"%.2f -> uv %.3f, %.3f) but has no eyes to turn (or [Eyes] is off); lids %.2f open\n")
				: "[eyes] %08X cannot see %08X's eyes from here (side %.2f, up %.2f, ahead %.2f): no glance\n",
				r.looker, r.glance.target, side, up, ahead, want.x, want.y, r.glance.lidsOpen);
			if (reach)
				now.insert(r.looker);
			std::lock_guard<std::mutex> guard(lookLock);
			Look& l = looks[r.looker];
			l.active = reach;
			l.lidsOpen = r.glance.lidsOpen;
		}
		std::lock_guard<std::mutex> guard(lookLock);
		for (auto it = looks.begin(); it != looks.end();) {
			Look& l = it->second;
			if (!now.count(it->first))
				l.active = false;
			float to = l.active ? 1.0f : 0.0f;
			l.weight += (to - l.weight) * (1.0f - std::exp(-kLidRate * dt));
			if (!l.active && l.weight < 0.01f)
				it = looks.erase(it);
			else
				++it;
		}
	}

	void HookEyes(float dt)
	{
		origEyes(dt);                              // the engine's own eyes first, every actor's
		ApplyGlances(dt < 0.0f ? 0.0f : (dt > 0.1f ? 0.1f : dt));
	}
}

unsigned long long EyeClockMs()
{
	LARGE_INTEGER now, freq;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&freq);
	return (unsigned long long)(now.QuadPart / (freq.QuadPart / 1000));
}

void LoadEyeConfig(INIReader& reader)
{
	enabled = reader.GetBoolean("Eyes", "enabled", true);
	glancesOn = reader.GetBoolean("Eyes", "glances", false);
	params.signX = reader.GetReal("Eyes", "signX", 1.0) < 0.0 ? -1.0f : 1.0f;
	params.signY = reader.GetReal("Eyes", "signY", 1.0) < 0.0 ? -1.0f : 1.0f;
	eyeRise = (float)reader.GetReal("Eyes", "eyeRise", eyeRise);
	eyeBack = (float)reader.GetReal("Eyes", "eyeBack", eyeBack);
	probe = reader.GetBoolean("Eyes", "probe", false);
	test = reader.GetBoolean("Eyes", "test", false);
	char key[96];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "eyes|config|%d|%d|%d|%d|%d|%d", (int)enabled, (int)glancesOn,
		(int)params.signX, (int)params.signY, (int)probe, (int)test);
	Note(key, "[eyes] [Eyes] enabled %d, glances told to Rapport %d, sign x %+d y %+d, eyes %.1f up / %.1f back "
		"from the mouth, probe %d, test %d\n", (int)enabled, (int)glancesOn, (int)params.signX, (int)params.signY,
		eyeRise, eyeBack, (int)probe, (int)test);
}

void InstallEyeHook()
{
	if (hooked || !enabled)
		return;
	// never patch a build we have not read: the eye update's prologue and both calls to it
	bool prologue = std::memcmp(reinterpret_cast<const void*>(eyeFn.GetUIntPtr()), kEyePrologue, sizeof(kEyePrologue)) == 0;
	bool calls = true;
	for (auto& c : eyeCalls) {
		const unsigned char* p = reinterpret_cast<const unsigned char*>(c.GetUIntPtr());
		calls = calls && p[0] == 0xE8 && c.GetUIntPtr() + 5 + *reinterpret_cast<const int32_t*>(p + 1) == eyeFn.GetUIntPtr();
	}
	if (!prologue || !calls) {
		Note("eyes|build", "[eyes] this game build is not the one the eyes were read from (prologue %d, calls %d): "
			"no glance turns an eye\n", (int)prologue, (int)calls);
		return;
	}
	if (!g_branchTrampoline.Create(1024 * 64)) {   // a no-op when the mouth made it already
		Note("eyes|hook", "[eyes] no room for a branch trampoline near the game: no glance turns an eye\n");
		return;
	}
	origEyes = reinterpret_cast<EyeFn>(eyeFn.GetUIntPtr());
	bool reaches = true;
	for (auto& c : eyeCalls) {
		if (!g_branchTrampoline.Write5Call(c.GetUIntPtr(), reinterpret_cast<uintptr_t>(&HookEyes))) {
			reaches = false;
			continue;
		}
		const unsigned char* p = reinterpret_cast<const unsigned char*>(c.GetUIntPtr());
		const unsigned char* s = reinterpret_cast<const unsigned char*>(c.GetUIntPtr() + 5 + *reinterpret_cast<const int32_t*>(p + 1));
		reaches = reaches && p[0] == 0xE8 && s[0] == 0xFF && s[1] == 0x25 && *reinterpret_cast<const uint32_t*>(s + 2) == 0 &&
			*reinterpret_cast<const uintptr_t*>(s + 6) == reinterpret_cast<uintptr_t>(&HookEyes);
	}
	hooked = reaches;
	Note("eyes|on", reaches ? "[eyes] the eye update's two calls hooked (its code untouched): glances turn eyes\n"
		: "[eyes] the eye update's calls are NOT both ours: glances may turn eyes only some frames\n");
}

bool EyesTurn()
{
	return hooked && enabled && glancesOn;
}

float EyeLidMax(unsigned int formID)
{
	std::lock_guard<std::mutex> guard(lookLock);
	auto it = looks.find(formID);
	if (it == looks.end())
		return 1.0f;
	return 1.0f - it->second.lidsOpen * it->second.weight;
}

void UpdateEyeProbe(const std::vector<ActorEntry>& actors, float dt)
{
	if (!probe && !test)
		return;
	Actor* player = *g_player;
	testClock += dt;
	bool fire = test && testClock >= 4.0f;
	if (fire)
		testClock = 0.0f;
	for (auto& e : actors) {
		Actor* a = e.actor;
		if (!a || a == player)
			continue;
		NiPoint3 pa, F, U, S;
		if (!EyesOf(a, pa, F, U, S))
			continue;
		if (probe && player) {
			Watch& w = watched[a->formID];
			w.since += dt;
			float side, up, ahead;
			BSShaderProperty* property = EyeProperty(a);
			if (w.since >= kProbeEvery && w.lines < kProbeLines && property && property->shaderMaterial &&
				Toward(a, player, side, up, ahead)) {
				w.since = 0.0f;
				float u, v;
				property->shaderMaterial->GetOffsetUV(&u, &v);
				char key[64];
				_snprintf_s(key, sizeof(key), _TRUNCATE, "eyes|probe|%08X|%d", a->formID, w.lines++);
				Note(key, "[eyes] probe %08X: engine uv (%.4f, %.4f); the player's eyes at side %.3f, up %.3f, ahead %.3f\n",
					a->formID, u, v, side, up, ahead);
			}
		}
		if (fire) {                            // the nearest other actor, within 150
			Actor* best = nullptr;
			float bestD = 150.0f;
			for (auto& o : actors) {
				NiPoint3 po, Fo, Uo, So;
				if (!o.actor || o.actor == a || !EyesOf(o.actor, po, Fo, Uo, So))
					continue;
				float d = Length(po - pa);
				if (d < bestD) {
					bestD = d;
					best = o.actor;
				}
			}
			if (best) {
				FaceAuthority::Glance g;
				g.target = best->formID;
				g.durationMs = 1500;
				g.lidsOpen = 1.0f;
				FaceAuthority::SetGlance(a->formID, g, EyeClockMs());
			}
		}
	}
}
