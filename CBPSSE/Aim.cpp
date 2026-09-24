// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24 (A-28). What it does and why: Aim.h.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Aim.h"

#include "ActorEntry.h"
#include "ActorUtils.h"
#include "AimSolve.h"
#include "CollisionHub.h"
#include "Mouth.h"
#include "SimObj.h"

#include "f4se/GameForms.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"
#include "f4se/NiNodes.h"
#include "f4se/NiObjects.h"
#include "f4se/NiTypes.h"
#include "f4se/PapyrusArgs.h"
#include "f4se/PapyrusNativeFunctions.h"
#include "f4se/PapyrusVM.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	using AimSolve::V3;

	bool enabled = false;
	std::vector<std::string> chainNames;          // root first, tip last
	AimSolve::Params params;
	NiPoint3 vaginaAt, vaginaIn, anusAt, anusIn;  // in Pelvis_skin's frame (physics_config.py writes them)
	bool vagina = false, anus = false, mouths = true;
	std::string anatomyBone = "AnatVulva";        // an actor carries our openings only with our bones
	const char* kPelvis = "Pelvis_skin";

	// AAF's busy actors, from Anatomy:Arousal's tick (AnatomyAim.SetBusy); old news counts as none
	std::mutex busyLock;
	std::unordered_set<UInt32> busy;
	ULONGLONG busyAt = 0;
	const ULONGLONG kBusyStaleMs = 10000;

	struct Held
	{
		AimSolve::State state;
		NiMatrix43 baseRot{};                     // the animation's own local rotation of the root
		NiMatrix43 wroteRot{};                    // what we wrote over it
		std::vector<NiPoint3> basePos, wrotePos;  // the children's local offsets, the same way
		bool wrote = false;
		bool toldKeyed = false;
		ULONGLONG seenAt = 0;
	};
	std::unordered_map<UInt32, Held> held;
	// Someone the scan no longer lists this long (out of OCBPC's reach, or unloaded) gets the animation's
	// pose back, found by form, and is let go. Not at once: a cell change skips one frame's scan.
	const ULONGLONG kUnseenMs = 500;

	LARGE_INTEGER lastTick{};

	// milliseconds from the performance counter (GetTickCount64 is past this project's Windows target)
	ULONGLONG NowMs()
	{
		LARGE_INTEGER c, f;
		QueryPerformanceCounter(&c);
		QueryPerformanceFrequency(&f);
		return (ULONGLONG)(c.QuadPart / (f.QuadPart / 1000));
	}

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

	bool ReadPoint(INIReader& reader, const char* key, NiPoint3& out)
	{
		auto parts = Split(reader.Get("Aim", key, ""), ',');
		if (parts.size() != 3)
			return false;
		out = NiPoint3(strtof(parts[0].c_str(), nullptr), strtof(parts[1].c_str(), nullptr), strtof(parts[2].c_str(), nullptr));
		return true;
	}

	float Radians(INIReader& reader, const char* key, float fallbackRadians)
	{
		return (float)reader.GetReal("Aim", key, fallbackRadians * 57.29578) / 57.29578f;
	}

	// The engine stores a rotation transposed: a local vector reaches the parent as stored^T * v (the
	// fork's Thing.cpp). AimSolve's matrices are the plain ones.
	AimSolve::M3 Actual(const NiMatrix43& stored)
	{
		AimSolve::M3 a;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				a.m[i][j] = stored.data[j][i];
		return a;
	}

	NiMatrix43 Stored(const AimSolve::M3& a)
	{
		NiMatrix43 s{};
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++)
				s.data[i][j] = a.m[j][i];
			s.data[i][3] = 0.0f;
		}
		return s;
	}

	AimSolve::M3 Mul(const AimSolve::M3& a, const AimSolve::M3& b)
	{
		AimSolve::M3 r;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
		return r;
	}

	V3 Apply(const AimSolve::M3& a, const V3& v)
	{
		return { a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
		         a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z };
	}

	V3 ToV3(const NiPoint3& p) { return { p.x, p.y, p.z }; }

	bool SameRot(const NiMatrix43& a, const NiMatrix43& b)
	{
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				if (a.data[i][j] != b.data[i][j])
					return false;
		return true;
	}

	bool SamePos(const NiPoint3& a, const NiPoint3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

	// The world transform of a node from its parent's and its own local one, as the engine computes it,
	// then the same down its children: the colliders built right after, and the skin, see the new pose.
	void UpdateWorldFrom(NiAVObject* node, const NiTransform& parent, int depth = 0)
	{
		if (!node || depth > 32)
			return;
		NiTransform& w = node->m_worldTransform;
		const NiTransform& l = node->m_localTransform;
		w.rot = l.rot * parent.rot;
		w.pos = parent.pos + parent.rot.Transpose() * (l.pos * parent.scale);
		w.scale = parent.scale * l.scale;
		if (NiNode* n = node->GetAsNiNode())
			for (UInt16 i = 0; i < n->m_children.m_emptyRunStart; i++)
				UpdateWorldFrom(n->m_children.m_data[i], w, depth + 1);
	}

	bool InScene(UInt32 formID, ULONGLONG now)
	{
		std::lock_guard<std::mutex> l(busyLock);
		return busyAt && now - busyAt < kBusyStaleMs && busy.count(formID) > 0;
	}

	// Our openings on a woman with our bones: the point and the inward axis from her Pelvis_skin.
	void AddAnatomyTargets(Actor* a, bool inScene, std::vector<AimSolve::Target>& out)
	{
		if ((!vagina && !anus) || actorUtils::IsActorMale(a))
			return;
		NiAVObject* root = a->unkF0->rootNode;
		BSFixedString pelvisName(kPelvis);
		NiAVObject* pelvis = root->GetObjectByName(&pelvisName);
		if (!pelvis)
			return;
		BSFixedString marker(anatomyBone.c_str());
		if (!pelvis->GetObjectByName(&marker))
			return;                                   // not one of ours: no opening to aim at
		const NiTransform& t = pelvis->m_worldTransform;
		NiMatrix43 toWorld = t.rot.Transpose();
		auto add = [&](int kind, const NiPoint3& at, const NiPoint3& in) {
			AimSolve::Target g;
			g.owner = a->formID;
			g.kind = kind;
			g.point = ToV3(t.pos + toWorld * (at * t.scale));
			g.in = AimSolve::Normalized(ToV3(toWorld * in));
			g.inScene = inScene;
			out.push_back(g);
		};
		if (vagina)
			add(AimSolve::kVagina, vaginaAt, vaginaIn);
		if (anus)
			add(AimSolve::kAnus, anusAt, anusIn);
	}

	// The chain's nodes under an actor's skeleton, root first; false unless every one is there.
	bool FindChain(Actor* a, NiAVObject*& root, std::vector<NiAVObject*>& kids)
	{
		kids.clear();
		if (!a || !a->unkF0 || !a->unkF0->rootNode || chainNames.size() < 2)
			return false;
		BSFixedString rootName(chainNames.front().c_str());
		root = a->unkF0->rootNode->GetObjectByName(&rootName);
		if (!root || !root->m_parent)
			return false;
		for (size_t k = 1; k < chainNames.size(); k++) {
			BSFixedString n(chainNames[k].c_str());
			NiAVObject* node = root->GetObjectByName(&n);   // below the root: a sibling chain is not ours
			if (!node)
				return false;
			kids.push_back(node);
		}
		return true;
	}

	// The FK walks root -> kids[0] -> kids[1] ...: true only if the skeleton hangs them that way.
	bool Linear(NiAVObject* root, const std::vector<NiAVObject*>& kids)
	{
		NiAVObject* up = root;
		for (auto* k : kids) {
			if (k->m_parent != up)
				return false;
			up = k;
		}
		return true;
	}

	void PutBack(Held& h, NiAVObject* root, const std::vector<NiAVObject*>& kids)
	{
		root->m_localTransform.rot = h.baseRot;
		for (size_t k = 0; k < kids.size() && k < h.basePos.size(); k++)
			kids[k]->m_localTransform.pos = h.basePos[k];
		if (root->m_parent)
			UpdateWorldFrom(root, root->m_parent->m_worldTransform);
		h.wrote = false;
	}

	// SetBusy(Actor[] akBusy): everyone in an AAF scene near the player, from Anatomy:Arousal's tick.
	void SetBusy(StaticFunctionTag*, VMArray<Actor*> actors)
	{
		std::unordered_set<UInt32> next;
		for (UInt32 i = 0; i < actors.Length(); i++) {
			Actor* a = nullptr;
			actors.Get(&a, i);
			if (a)
				next.insert(a->formID);
		}
		std::lock_guard<std::mutex> l(busyLock);
		busy.swap(next);
		busyAt = NowMs();
	}
}

void LoadAimConfig(INIReader& reader)
{
	enabled = reader.GetBoolean("Aim", "enabled", false);
	chainNames = Split(reader.Get("Aim", "chain", ""), '|');
	params.requireScene = reader.GetBoolean("Aim", "requireScene", true);
	params.captureAngle = Radians(reader, "captureAngle", params.captureAngle);
	params.keepAngle = (std::max)(params.captureAngle, Radians(reader, "keepAngle", params.keepAngle));
	params.entryAngle = Radians(reader, "entryAngle", params.entryAngle);
	params.reach = (std::max)(0.1f, (float)reader.GetReal("Aim", "reach", params.reach));
	params.minReach = (float)reader.GetReal("Aim", "minReach", params.minReach);
	params.depth = (float)reader.GetReal("Aim", "depth", params.depth);
	params.minInside = (float)reader.GetReal("Aim", "minInside", params.minInside);
	params.maxStretch = (std::max)(1.0f, (float)reader.GetReal("Aim", "maxStretch", params.maxStretch));
	params.rate = (std::max)(0.5f, (float)reader.GetReal("Aim", "rate", params.rate));
	vagina = ReadPoint(reader, "vagina", vaginaAt) && ReadPoint(reader, "vaginaIn", vaginaIn);
	anus = ReadPoint(reader, "anus", anusAt) && ReadPoint(reader, "anusIn", anusIn);
	mouths = reader.GetBoolean("Aim", "mouths", true);
	anatomyBone = reader.Get("Aim", "anatomyBone", anatomyBone);
	if (chainNames.size() < 2)
		enabled = false;                              // a root and at least a tip
	for (auto& n : chainNames) {
		for (auto& b : boneNames) {
			if (_stricmp(n.c_str(), b.c_str()) == 0)
				Note("aim|physics|" + n, "[aim] %s is also a physics bone ([Attach]): OCBPC writes it after the aim "
					"each frame, so the aim cannot show on it\n", n.c_str());
		}
	}
	char key[96];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "aim|config|%d|%d|%d|%d|%d", (int)enabled, (int)chainNames.size(), (int)vagina,
		(int)anus, (int)mouths);
	Note(key, "[aim] %s: chain of %d (%s ... %s), vagina %d, anus %d, mouths %d, scene required %d; capture %.0f, keep %.0f, "
		"entry %.0f degrees, reach %.2f x, depth %.1f, stretch up to %.2f\n", enabled ? "on" : "off", (int)chainNames.size(),
		chainNames.empty() ? "-" : chainNames.front().c_str(), chainNames.empty() ? "-" : chainNames.back().c_str(), (int)vagina,
		(int)anus, (int)mouths, (int)params.requireScene, params.captureAngle * 57.29578f, params.keepAngle * 57.29578f,
		params.entryAngle * 57.29578f, params.reach, params.depth, params.maxStretch);
}

void ResetAims()
{
	held.clear();
	std::lock_guard<std::mutex> l(busyLock);
	busy.clear();
	busyAt = 0;
}

void UpdateAims()
{
	if (!enabled && held.empty())
		return;
	LARGE_INTEGER now, freq;
	QueryPerformanceCounter(&now);
	QueryPerformanceFrequency(&freq);
	float dt = lastTick.QuadPart ? (float)(now.QuadPart - lastTick.QuadPart) / (float)freq.QuadPart : 0.0f;
	lastTick = now;
	dt = (std::min)((std::max)(dt, 0.0f), 0.1f);
	ULONGLONG ms = NowMs();

	// who is in a scene: read once per actor this frame, so the openings and the chains agree
	std::unordered_map<UInt32, bool> sceneNow;
	auto inSceneNow = [&](UInt32 formID) {
		auto it = sceneNow.find(formID);
		if (it != sceneNow.end())
			return it->second;
		bool v = InScene(formID, ms);
		sceneNow[formID] = v;
		return v;
	};

	// every opening in reach: ours on the women, and every mouth
	std::vector<AimSolve::Target> targets;
	if (enabled) {
		for (auto& e : actorEntries) {
			Actor* a = e.actor;
			if (!a || !a->unkF0 || !a->unkF0->rootNode)
				continue;
			bool inScene = inSceneNow(a->formID);
			AddAnatomyTargets(a, inScene, targets);
			NiPoint3 m, out;
			if (mouths && MouthOpening(a, m, out)) {
				AimSolve::Target g;
				g.owner = a->formID;
				g.kind = AimSolve::kMouth;
				g.point = ToV3(m);
				g.in = AimSolve::Normalized(ToV3(out * -1.0f));
				g.inScene = inScene;
				targets.push_back(g);
			}
		}
	}

	// every chain: the animation's pose, the correction on top, written back
	for (auto& e : actorEntries) {
		Actor* a = e.actor;
		NiAVObject* root = nullptr;
		std::vector<NiAVObject*> kids;
		if (!FindChain(a, root, kids))
			continue;
		if (!Linear(root, kids)) {
			Note("aim|shape|" + std::to_string(a->formID), "[aim] %08X: %s ... %s do not hang one from the next - "
				"that chain is not aimed\n", a->formID, chainNames.front().c_str(), chainNames.back().c_str());
			held.erase(a->formID);
			continue;
		}
		auto found = held.find(a->formID);
		if (!enabled && found == held.end())
			continue;
		Held& h = held[a->formID];
		h.seenAt = ms;

		// the animation's pose: what is there now, unless it is still exactly what we wrote (then the
		// animation did not key it this frame, and ours must not be taken for the animation's)
		NiMatrix43 cur = root->m_localTransform.rot;
		if (h.wrote && SameRot(cur, h.wroteRot)) {
			if (!h.toldKeyed) {
				Note("aim|keyed|" + std::to_string(a->formID) + "|0", "[aim] %08X: nothing keys %s between frames - the "
					"correction rides on the pose the animation last set\n", a->formID, chainNames.front().c_str());
				h.toldKeyed = true;
			}
		}
		else {
			if (h.wrote && !h.toldKeyed) {
				Note("aim|keyed|" + std::to_string(a->formID) + "|1", "[aim] %08X: the animation keys %s every frame - the "
					"correction is laid on its pose each frame\n", a->formID, chainNames.front().c_str());
				h.toldKeyed = true;
			}
			h.baseRot = cur;
		}
		h.basePos.resize(kids.size());
		for (size_t k = 0; k < kids.size(); k++) {
			const NiPoint3& p = kids[k]->m_localTransform.pos;
			if (!(h.wrote && k < h.wrotePos.size() && SamePos(p, h.wrotePos[k])))
				h.basePos[k] = p;
		}

		if (!enabled) {                               // switched off: put the animation's pose back, once
			if (h.wrote)
				PutBack(h, root, kids);
			held.erase(a->formID);
			continue;
		}

		// the chain as the animation put it, from the parent's world transform down
		const NiTransform& parent = root->m_parent->m_worldTransform;
		AimSolve::M3 P = Actual(parent.rot);
		AimSolve::M3 Qbase = Actual(h.baseRot);
		V3 base = AimSolve::Add(ToV3(parent.pos), Apply(P, AimSolve::Scale(ToV3(root->m_localTransform.pos), parent.scale)));
		AimSolve::M3 Q = Mul(P, Qbase);
		float s = parent.scale * root->m_localTransform.scale;
		V3 at = base;
		float length = 0.0f;
		for (size_t k = 0; k < kids.size(); k++) {
			V3 next = AimSolve::Add(at, Apply(Q, AimSolve::Scale(ToV3(h.basePos[k]), s)));
			length += AimSolve::Length(AimSolve::Sub(next, at));
			at = next;
			Q = Mul(Q, Actual(kids[k]->m_localTransform.rot));
			s *= kids[k]->m_localTransform.scale;
		}
		AimSolve::Chain c;
		c.owner = a->formID;
		c.inScene = inSceneNow(a->formID);
		c.base = base;
		c.tip = at;
		c.length = length;
		c.parent = AimSolve::FromMatrix(P);

		AimSolve::Result r = AimSolve::Update(h.state, c, targets, params, dt);
		if (r.newLock) {
			char key[96];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "aim|lock|%08X|%08X|%d", a->formID, r.targetOwner, r.targetKind);
			Note(key, "[aim] %08X: shaft onto %08X's %s, %.1f degrees off, stretch %.2f\n", a->formID, r.targetOwner,
				AimSolve::KindName(r.targetKind), r.angle * 57.29578f, r.stretch);
		}
		if (r.active) {
			root->m_localTransform.rot = Stored(Mul(AimSolve::ToMatrix(r.local), Qbase));
			h.wrotePos.resize(kids.size());
			for (size_t k = 0; k < kids.size(); k++) {
				kids[k]->m_localTransform.pos = h.basePos[k] * r.stretch;
				h.wrotePos[k] = kids[k]->m_localTransform.pos;
			}
			h.wroteRot = root->m_localTransform.rot;
			h.wrote = true;
			UpdateWorldFrom(root, parent);
		}
		else if (h.wrote) {
			PutBack(h, root, kids);
		}
	}

	for (auto it = held.begin(); it != held.end();) {
		if (ms - it->second.seenAt <= kUnseenMs) {
			++it;
			continue;
		}
		if (it->second.wrote) {
			Actor* a = DYNAMIC_CAST(LookupFormByID(it->first), TESForm, Actor);
			NiAVObject* root = nullptr;
			std::vector<NiAVObject*> kids;
			if (FindChain(a, root, kids) && Linear(root, kids)) {
				PutBack(it->second, root, kids);
				Note("aim|unseen|" + std::to_string(it->first), "[aim] %08X: out of the scan with a correction on - the "
					"animation's pose put back\n", it->first);
			}
		}
		it = held.erase(it);                           // unloaded: its next skeleton is a fresh one
	}
}

bool RegisterAimFuncs(VirtualMachine* vm)
{
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, VMArray<Actor*>>("SetBusy", "AnatomyAim", SetBusy, vm));
	vm->SetFunctionFlags("AnatomyAim", "SetBusy", IFunction::kFunctionFlag_NoWait);
	return true;
}
