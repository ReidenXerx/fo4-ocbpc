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
	std::vector<std::string> handNames;           // knuckle bones: a hand there holds a shaft
	std::vector<std::string> gripSides;           // LArm, RArm: <side>_Finger21..53 make a grip
	AimSolve::Params params;
	// Her openings, in Pelvis_skin's frame (physics_config.py writes them): entrance, inward axis, the
	// path inside. The throat, in HEAD's frame, per sex; the entrance is [Mouth]'s mouth.
	NiPoint3 vaginaAt, vaginaIn, anusAt, anusIn;
	std::vector<NiPoint3> vaginaPath, anusPath, throatF, throatM;
	bool vagina = false, anus = false, mouths = true;
	// The shaft enters a mouth this far BELOW the line where her lips meet: centred on that line its upper
	// half rode over her upper lip and into her cheek and nose (the owner's look, 2026-09-24). With its axis
	// a radius down, its top runs under her upper lip and the contact mouth drops her jaw around the rest.
	float mouthDrop = 1.3f;
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
		std::vector<NiMatrix43> baseRot, wroteRot;   // per joint but the tip: the animation's, and ours
		std::vector<NiPoint3> basePos, wrotePos;     // per joint after the root: its offset, the same way
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

	bool ParsePoint(const std::string& text, NiPoint3& out)
	{
		auto parts = Split(text, ',');
		if (parts.size() != 3)
			return false;
		out = NiPoint3(strtof(parts[0].c_str(), nullptr), strtof(parts[1].c_str(), nullptr), strtof(parts[2].c_str(), nullptr));
		return true;
	}

	bool ReadPoint(INIReader& reader, const char* key, NiPoint3& out)
	{
		return ParsePoint(reader.Get("Aim", key, ""), out);
	}

	// x,y,z;x,y,z;... (a malformed point ends the list there)
	std::vector<NiPoint3> ReadPath(INIReader& reader, const char* key)
	{
		std::vector<NiPoint3> out;
		for (auto& p : Split(reader.Get("Aim", key, ""), ';')) {
			NiPoint3 q;
			if (!ParsePoint(p, q))
				break;
			out.push_back(q);
		}
		return out;
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

	NiAVObject* Find(NiAVObject* under, const std::string& name)
	{
		BSFixedString n(name.c_str());
		return under ? under->GetObjectByName(&n) : nullptr;
	}

	// A node's local point in the world.
	V3 WorldPoint(const NiTransform& t, const NiPoint3& local)
	{
		return ToV3(t.pos + t.rot.Transpose() * (local * t.scale));
	}

	std::vector<V3> WorldPath(const NiTransform& t, const std::vector<NiPoint3>& local)
	{
		std::vector<V3> out;
		for (auto& p : local)
			out.push_back(WorldPoint(t, p));
		return out;
	}

	// Our openings on a woman with our bones: entrance, inward axis and path from her Pelvis_skin.
	void AddAnatomyTargets(Actor* a, bool inScene, std::vector<AimSolve::Target>& out)
	{
		if ((!vagina && !anus) || actorUtils::IsActorMale(a))
			return;
		NiAVObject* pelvis = Find(a->unkF0->rootNode, kPelvis);
		if (!pelvis || !Find(pelvis, anatomyBone))
			return;                                   // not one of ours: no opening to aim at
		const NiTransform& t = pelvis->m_worldTransform;
		NiMatrix43 toWorld = t.rot.Transpose();
		auto add = [&](int kind, const NiPoint3& at, const NiPoint3& in, const std::vector<NiPoint3>& path) {
			AimSolve::Target g;
			g.owner = a->formID;
			g.kind = kind;
			g.point = WorldPoint(t, at);
			g.in = AimSolve::Normalized(ToV3(toWorld * in));
			g.path = WorldPath(t, path);
			g.inScene = inScene;
			out.push_back(g);
		};
		if (vagina)
			add(AimSolve::kVagina, vaginaAt, vaginaIn, vaginaPath);
		if (anus)
			add(AimSolve::kAnus, anusAt, anusIn, anusPath);
	}

	void AddMouthTarget(Actor* a, bool inScene, std::vector<AimSolve::Target>& out)
	{
		NiPoint3 m, outward, up;
		if (!mouths || !MouthOpening(a, m, outward, &up))
			return;
		V3 down = AimSolve::Scale(ToV3(up), -mouthDrop);
		AimSolve::Target g;
		g.owner = a->formID;
		g.kind = AimSolve::kMouth;
		g.point = AimSolve::Add(ToV3(m), down);
		g.in = AimSolve::Normalized(ToV3(outward * -1.0f));
		if (NiAVObject* head = Find(a->unkF0->rootNode, "HEAD"))
			for (auto& q : WorldPath(head->m_worldTransform, actorUtils::IsActorMale(a) ? throatM : throatF))
				g.path.push_back(AimSolve::Add(q, down));   // the throat, lowered with the entrance
		g.inScene = inScene;
		out.push_back(g);
	}

	// A hand as an opening: the grip is the middle of its four fingers' joints (around a shaft they close
	// into a ring), along the line of its knuckles, index to little finger. Entered either way (AimSolve).
	void AddGripTargets(Actor* a, bool inScene, std::vector<AimSolve::Target>& out)
	{
		for (auto& side : gripSides) {
			V3 sum{};
			int found = 0;
			V3 index{}, little{};
			for (int f = 2; f <= 5; f++) {
				for (int j = 1; j <= 3; j++) {
					NiAVObject* n = Find(a->unkF0->rootNode, side + "_Finger" + std::to_string(f * 10 + j));
					if (!n)
						continue;
					V3 at = ToV3(n->m_worldTransform.pos);
					sum = AimSolve::Add(sum, at);
					found++;
					if (j == 1 && f == 2)
						index = at;
					if (j == 1 && f == 5)
						little = at;
				}
			}
			if (found != 12)
				continue;
			AimSolve::Target g;
			g.owner = a->formID;
			g.kind = AimSolve::kHand;
			g.point = AimSolve::Scale(sum, 1.0f / 12.0f);
			g.in = AimSolve::Normalized(AimSolve::Sub(little, index));
			g.inScene = inScene;
			out.push_back(g);
		}
	}

	// The chain's nodes under an actor's skeleton, root first; false unless every one is there.
	bool FindChain(Actor* a, std::vector<NiAVObject*>& nodes)
	{
		nodes.clear();
		if (!a || !a->unkF0 || !a->unkF0->rootNode || chainNames.size() < 2)
			return false;
		NiAVObject* root = Find(a->unkF0->rootNode, chainNames.front());
		if (!root || !root->m_parent)
			return false;
		nodes.push_back(root);
		for (size_t k = 1; k < chainNames.size(); k++) {
			NiAVObject* node = Find(root, chainNames[k]);   // below the root: a sibling chain is not ours
			if (!node)
				return false;
			nodes.push_back(node);
		}
		return true;
	}

	// The pose is walked root -> nodes[1] -> nodes[2] ...: true only if the skeleton hangs them that way.
	bool Linear(const std::vector<NiAVObject*>& nodes)
	{
		for (size_t k = 1; k < nodes.size(); k++)
			if (nodes[k]->m_parent != nodes[k - 1])
				return false;
		return true;
	}

	void PutBack(Held& h, const std::vector<NiAVObject*>& nodes)
	{
		for (size_t i = 0; i + 1 < nodes.size() && i < h.baseRot.size(); i++)
			nodes[i]->m_localTransform.rot = h.baseRot[i];
		for (size_t k = 1; k < nodes.size() && k - 1 < h.basePos.size(); k++)
			nodes[k]->m_localTransform.pos = h.basePos[k - 1];
		UpdateWorldFrom(nodes[0], nodes[0]->m_parent->m_worldTransform);
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
	handNames = Split(reader.Get("Aim", "hands", "LArm_Finger31|RArm_Finger31"), '|');
	gripSides = Split(reader.Get("Aim", "grips", "LArm|RArm"), '|');
	params.requireScene = reader.GetBoolean("Aim", "requireScene", true);
	params.captureAngle = Radians(reader, "captureAngle", params.captureAngle);
	params.keepAngle = (std::max)(params.captureAngle, Radians(reader, "keepAngle", params.keepAngle));
	params.captureMiss = (float)reader.GetReal("Aim", "captureMiss", params.captureMiss);
	params.keepMiss = (std::max)(params.captureMiss, (float)reader.GetReal("Aim", "keepMiss", params.keepMiss));
	params.entryAngle = Radians(reader, "entryAngle", params.entryAngle);
	params.reach = (std::max)(0.1f, (float)reader.GetReal("Aim", "reach", params.reach));
	params.minReach = (float)reader.GetReal("Aim", "minReach", params.minReach);
	params.depth = (float)reader.GetReal("Aim", "depth", params.depth);
	params.minInside = (float)reader.GetReal("Aim", "minInside", params.minInside);
	params.maxStretch = (std::max)(1.0f, (float)reader.GetReal("Aim", "maxStretch", params.maxStretch));
	params.rate = (std::max)(0.5f, (float)reader.GetReal("Aim", "rate", params.rate));
	params.handRadius = (float)reader.GetReal("Aim", "handRadius", params.handRadius);
	params.handHold = (std::max)(0.0f, (float)reader.GetReal("Aim", "handHold", params.handHold));
	vagina = ReadPoint(reader, "vagina", vaginaAt) && ReadPoint(reader, "vaginaIn", vaginaIn);
	anus = ReadPoint(reader, "anus", anusAt) && ReadPoint(reader, "anusIn", anusIn);
	vaginaPath = ReadPath(reader, "vaginaPath");
	anusPath = ReadPath(reader, "anusPath");
	throatF = ReadPath(reader, "throatF");
	throatM = ReadPath(reader, "throatM");
	mouths = reader.GetBoolean("Aim", "mouths", true);
	mouthDrop = (float)reader.GetReal("Aim", "mouthDrop", mouthDrop);
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
	char key[128];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "aim|config|%d|%d|%d|%d|%d|%d|%d|%d", (int)enabled, (int)chainNames.size(),
		(int)vagina, (int)anus, (int)mouths, (int)vaginaPath.size(), (int)anusPath.size(), (int)throatF.size());
	Note(key, "[aim] %s: chain of %d (%s ... %s), vagina %d (path %d), anus %d (path %d), mouths %d (throat %d/%d), "
		"knuckles %d, grips %d, scene required %d; capture %.0f / keep %.0f degrees, miss %.1f / %.1f, entry %.0f, reach %.2f x, "
		"stretch up to %.2f\n", enabled ? "on" : "off", (int)chainNames.size(), chainNames.empty() ? "-" : chainNames.front().c_str(),
		chainNames.empty() ? "-" : chainNames.back().c_str(), (int)vagina, (int)vaginaPath.size(), (int)anus, (int)anusPath.size(),
		(int)mouths, (int)throatF.size(), (int)throatM.size(), (int)handNames.size(), (int)gripSides.size(), (int)params.requireScene,
		params.captureAngle * 57.29578f, params.keepAngle * 57.29578f, params.captureMiss, params.keepMiss,
		params.entryAngle * 57.29578f, params.reach, params.maxStretch);
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

	// every opening in reach (ours on the women, every mouth) and every hand
	std::vector<AimSolve::Target> targets;
	std::vector<AimSolve::Hand> hands;
	if (enabled) {
		for (auto& e : actorEntries) {
			Actor* a = e.actor;
			if (!a || !a->unkF0 || !a->unkF0->rootNode)
				continue;
			bool inScene = inSceneNow(a->formID);
			AddAnatomyTargets(a, inScene, targets);
			AddMouthTarget(a, inScene, targets);
			if (!inScene && params.requireScene)
				continue;                             // a hand out of a scene holds nobody's shaft here
			AddGripTargets(a, inScene, targets);
			for (auto& n : handNames)
				if (NiAVObject* h = Find(a->unkF0->rootNode, n))
					hands.push_back(AimSolve::Hand{ a->formID, ToV3(h->m_worldTransform.pos) });
		}
	}

	// every chain: the animation's pose, the corrections on top, written back
	for (auto& e : actorEntries) {
		Actor* a = e.actor;
		std::vector<NiAVObject*> nodes;
		if (!FindChain(a, nodes))
			continue;
		if (!Linear(nodes)) {
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
		size_t n = nodes.size();

		// the animation's pose: what is there now, unless it is still exactly what we wrote (then the
		// animation did not key it this frame, and ours must not be taken for the animation's)
		h.baseRot.resize(n - 1);
		h.basePos.resize(n - 1);
		bool keyed = false;
		for (size_t i = 0; i + 1 < n; i++) {
			const NiMatrix43& cur = nodes[i]->m_localTransform.rot;
			if (!(h.wrote && i < h.wroteRot.size() && SameRot(cur, h.wroteRot[i]))) {
				keyed = keyed || h.wrote;
				h.baseRot[i] = cur;
			}
		}
		for (size_t k = 1; k < n; k++) {
			const NiPoint3& cur = nodes[k]->m_localTransform.pos;
			if (!(h.wrote && k - 1 < h.wrotePos.size() && SamePos(cur, h.wrotePos[k - 1])))
				h.basePos[k - 1] = cur;
		}
		if (h.wrote && !h.toldKeyed) {
			Note("aim|keyed|" + std::to_string(a->formID) + (keyed ? "|1" : "|0"), keyed ?
				"[aim] %08X: the animation keys %s every frame - the correction is laid on its pose each frame\n" :
				"[aim] %08X: nothing keys %s between frames - the correction rides on the pose the animation last set\n",
				a->formID, chainNames.front().c_str());
			h.toldKeyed = true;
		}

		if (!enabled) {                               // switched off: put the animation's pose back, once
			if (h.wrote)
				PutBack(h, nodes);
			held.erase(a->formID);
			continue;
		}

		// the chain as the animation put it: locals, and offsets in world units
		const NiTransform& parent = nodes[0]->m_parent->m_worldTransform;
		AimSolve::Chain c;
		c.owner = a->formID;
		c.inScene = inSceneNow(a->formID);
		c.parent = AimSolve::FromMatrix(Actual(parent.rot));
		c.root = WorldPoint(parent, nodes[0]->m_localTransform.pos);
		c.locals.resize(n);
		c.offsets.assign(n, V3{});
		float scale = parent.scale * nodes[0]->m_localTransform.scale;
		for (size_t i = 0; i < n; i++) {
			const NiMatrix43& rot = i + 1 < n ? h.baseRot[i] : nodes[i]->m_localTransform.rot;
			c.locals[i] = AimSolve::FromMatrix(Actual(rot));
			if (i > 0) {
				c.offsets[i] = AimSolve::Scale(ToV3(h.basePos[i - 1]), scale);
				scale *= nodes[i]->m_localTransform.scale;
			}
		}

		std::uint32_t wasOwner = h.state.lockedOwner;
		int wasKind = h.state.lockedKind;
		AimSolve::Result r = AimSolve::Update(h.state, c, targets, hands, params, dt);
		if (r.released) {
			char key[128];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "aim|release|%08X|%08X|%d|%s", a->formID, wasOwner, wasKind, r.why);
			Note(key, "[aim] %08X: let go of %08X's %s: %s\n", a->formID, wasOwner, AimSolve::KindName(wasKind), r.why);
		}
		if (r.held) {
			Note("aim|held|" + std::to_string(a->formID), "[aim] %08X: a hand holds the shaft - it is not aimed while "
				"held\n", a->formID);
		}
		if (r.newLock) {
			char key[96];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "aim|lock|%08X|%08X|%d", a->formID, r.targetOwner, r.targetKind);
			Note(key, "[aim] %08X: shaft onto %08X's %s, %.1f degrees and %.1f off, stretch %.2f\n", a->formID,
				r.targetOwner, AimSolve::KindName(r.targetKind), r.angle * 57.29578f, r.miss, r.stretch);
		}
		if (r.active) {
			h.wroteRot.resize(n - 1);
			h.wrotePos.resize(n - 1);
			for (size_t i = 0; i + 1 < n; i++) {
				nodes[i]->m_localTransform.rot = Stored(Mul(AimSolve::ToMatrix(r.local[i]), Actual(h.baseRot[i])));
				h.wroteRot[i] = nodes[i]->m_localTransform.rot;
			}
			for (size_t k = 1; k < n; k++) {
				nodes[k]->m_localTransform.pos = h.basePos[k - 1] * r.stretch;
				h.wrotePos[k - 1] = nodes[k]->m_localTransform.pos;
			}
			h.wrote = true;
			UpdateWorldFrom(nodes[0], parent);
		}
		else if (h.wrote) {
			PutBack(h, nodes);
		}
	}

	for (auto it = held.begin(); it != held.end();) {
		if (ms - it->second.seenAt <= kUnseenMs) {
			++it;
			continue;
		}
		if (it->second.wrote) {
			Actor* a = DYNAMIC_CAST(LookupFormByID(it->first), TESForm, Actor);
			std::vector<NiAVObject*> nodes;
			if (FindChain(a, nodes) && Linear(nodes)) {
				PutBack(it->second, nodes);
				Note("aim|unseen|" + std::to_string(it->first), "[aim] %08X: out of the scan with a correction on - the "
					"animation's pose put back\n", it->first);
			}
		}
		it = held.erase(it);                           // unloaded: its next skeleton is a fresh one
	}
}

bool AimSeesScene(unsigned int formID)
{
	return InScene(formID, NowMs());
}

bool RegisterAimFuncs(VirtualMachine* vm)
{
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, VMArray<Actor*>>("SetBusy", "AnatomyAim", SetBusy, vm));
	vm->SetFunctionFlags("AnatomyAim", "SetBusy", IFunction::kFunctionFlag_NoWait);
	return true;
}
