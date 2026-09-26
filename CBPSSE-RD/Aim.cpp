// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24 (A-28). What it does and why: Aim.h.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Aim.h"

#include "Game.h"
#include "ActorEntry.h"
#include "ActorUtils.h"
#include "AimSolve.h"
#include "CollisionHub.h"
#include "FaceAuthority.h"
#include "Mouth.h"
#include "SimObj.h"


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
	// path inside. The throat per sex, the entrance [Mouth]'s mouth: its part in the mouth in HEAD's frame,
	// its part down the neck in Neck's, so it bends where her neck bends (her head thrown back swung a
	// HEAD-only path forward out of her throat: the owner's x-ray, 2026-09-25).
	NiPoint3 vaginaAt, vaginaIn, anusAt, anusIn;
	std::vector<NiPoint3> vaginaPath, anusPath, throatF, throatM, throatNeckF, throatNeckM;
	bool vagina = false, anus = false, mouths = true;
	// The shaft enters a mouth this far BELOW the line where her lips meet: centred on that line its upper
	// half rode over her upper lip and into her cheek and nose (the owner's look, 2026-09-24). With its axis
	// a radius down, its top runs under her upper lip and the contact mouth drops her jaw around the rest.
	float mouthDrop = 1.3f;
	// ... and it turns onto her mouth's own axis this far IN FRONT of her lips (A-32): straight from his hips
	// to her lips, a shaft crossed them at the animation's slant, cutting an oval wider than her mouth opens,
	// and her mouth's corner clipped it (the owner's look, 2026-09-25). Along the axis it cuts a circle.
	float mouthLead = 1.5f;
	// [Aim] probe=1 (dev): once a second per shaft and mouth in reach, what a new lock would say, and where the
	// animation's tip is against her mouth (the owner's lying blowjob, 2026-09-26: the shaft through her chin)
	bool probe = false;
	std::unordered_map<std::uint64_t, std::pair<ULONGLONG, int>> probed;   // pair -> (last line, lines)
	std::string anatomyBone = "AnatVulva";        // an actor carries our openings only with our bones
	const char* kPelvis = "Pelvis_skin";
	// The shape ([Shape], A-31): every chain's shaft this thin and its head this big, one head size per man
	bool shapeOn = false;
	float shaftScale = 1.0f, headLo = 1.0f, headHi = 1.0f;

	// AAF's busy actors, from Anatomy:Arousal's tick (AnatomyAim.SetBusy); old news counts as none
	std::mutex busyLock;
	std::unordered_map<UInt32, float> depths;      // AimDepth: this frame's (the scan thread's own, like UpdateMouths)
	std::unordered_set<UInt32> busy;
	ULONGLONG busyAt = 0;
	const ULONGLONG kBusyStaleMs = 10000;

	struct Held
	{
		AimSolve::State state;
		std::vector<NiMatrix43> baseRot, wroteRot;   // per joint but the tip: the animation's, and ours
		std::vector<NiPoint3> basePos, wrotePos;     // per joint after the root: its offset, the same way
		std::vector<float> baseScale, wroteScale;    // per node: its local scale, the same way (the shape)
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
		NiTransform& w = G::World(node);
		const NiTransform& l = G::Local(node);
		w.rot = l.rot * parent.rot;
		w.pos = parent.pos + parent.rot.Transpose() * (l.pos * parent.scale);
		w.scale = parent.scale * l.scale;
		if (NiNode* n = G::AsNode(node))
			for (UInt16 i = 0; i < G::ChildCount(n); i++)
				UpdateWorldFrom(G::Child(n, i), w, depth + 1);
	}

	bool InScene(UInt32 formID, ULONGLONG now)
	{
		std::lock_guard<std::mutex> l(busyLock);
		return busyAt && now - busyAt < kBusyStaleMs && busy.count(formID) > 0;
	}

	NiAVObject* Find(NiAVObject* under, const std::string& name)
	{
		BSFixedString n(name.c_str());
		return under ? under->GetObjectByName(n) : nullptr;
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
		NiAVObject* pelvis = Find(G::Root(a), kPelvis);
		if (!pelvis || !Find(pelvis, anatomyBone))
			return;                                   // not one of ours: no opening to aim at
		const NiTransform& t = G::World(pelvis);
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
		V3 lips = AimSolve::Add(ToV3(m), down);
		g.in = AimSolve::Normalized(ToV3(outward * -1.0f));
		g.point = AimSolve::Add(lips, AimSolve::Scale(g.in, -mouthLead));   // in front of her lips, on her axis
		if (mouthLead > 0.0f)
			g.path.push_back(lips);                     // then through them along it
		bool male = actorUtils::IsActorMale(a);
		if (NiAVObject* head = Find(G::Root(a), "HEAD")) {
			for (auto& q : WorldPath(G::World(head), male ? throatM : throatF))
				g.path.push_back(AimSolve::Add(q, down));   // in the mouth, lowered with the entrance
			if (NiAVObject* neck = Find(G::Root(a), "Neck"))
				for (auto& q : WorldPath(G::World(neck), male ? throatNeckM : throatNeckF))
					g.path.push_back(q);                    // down the neck: where the neck is, not the head
		}
		g.inScene = inScene;
		out.push_back(g);
	}

	// A hand as an opening: the grip is the centre its curled fingers wrap around (AimSolve::GripCentre),
	// along the line of its knuckles, index to little finger. Entered either way (AimSolve).
	const float kGripMaxRadius = 4.0f;               // a finger around a shaft (1.55) curls far tighter
	void AddGripTargets(Actor* a, bool inScene, std::vector<AimSolve::Target>& out)
	{
		for (auto& side : gripSides) {
			V3 joints[4][3];
			int found = 0;
			for (int f = 2; f <= 5; f++) {
				for (int j = 1; j <= 3; j++) {
					NiAVObject* n = Find(G::Root(a), side + "_Finger" + std::to_string(f * 10 + j));
					if (!n)
						continue;
					joints[f - 2][j - 1] = ToV3(G::World(n).pos);
					found++;
				}
			}
			V3 centre;
			if (found != 12 || !AimSolve::GripCentre(joints, kGripMaxRadius, centre))
				continue;                                 // an open hand is not a grip
			AimSolve::Target g;
			g.owner = a->formID;
			g.kind = AimSolve::kHand;
			g.point = centre;
			g.in = AimSolve::Normalized(AimSolve::Sub(joints[3][0], joints[0][0]));
			g.inScene = inScene;
			out.push_back(g);
		}
	}

	// The chain's nodes under an actor's skeleton, root first; false unless every one is there.
	bool FindChain(Actor* a, std::vector<NiAVObject*>& nodes)
	{
		nodes.clear();
		if (!a || !G::Root(a) || !G::Root(a) || chainNames.size() < 2)
			return false;
		NiAVObject* root = Find(G::Root(a), chainNames.front());
		if (!root || !G::Parent(root))
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
			if (G::Parent(nodes[k]) != nodes[k - 1])
				return false;
		return true;
	}

	void PutBack(Held& h, const std::vector<NiAVObject*>& nodes)
	{
		for (size_t i = 0; i + 1 < nodes.size() && i < h.baseRot.size(); i++)
			G::Local(nodes[i]).rot = h.baseRot[i];
		for (size_t k = 1; k < nodes.size() && k - 1 < h.basePos.size(); k++)
			G::Local(nodes[k]).pos = h.basePos[k - 1];
		for (size_t i = 0; i < nodes.size() && i < h.baseScale.size(); i++)
			G::Local(nodes[i]).scale = h.baseScale[i];
		UpdateWorldFrom(nodes[0], G::World(G::Parent(nodes[0])));
		h.wrote = false;
	}

	// SetBusy(Actor[] akBusy): everyone in an AAF scene near the player, from Anatomy:Arousal's tick.
	void SetBusy(std::monostate, std::vector<Actor*> actors)
	{
		std::unordered_set<UInt32> next;
		for (Actor* a : actors) {
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
	throatNeckF = ReadPath(reader, "throatNeckF");
	throatNeckM = ReadPath(reader, "throatNeckM");
	mouths = reader.GetBoolean("Aim", "mouths", true);
	mouthDrop = (float)reader.GetReal("Aim", "mouthDrop", mouthDrop);
	mouthLead = (std::max)(0.0f, (float)reader.GetReal("Aim", "mouthLead", mouthLead));
	probe = reader.GetBoolean("Aim", "probe", false);
	anatomyBone = reader.Get("Aim", "anatomyBone", anatomyBone);
	shaftScale = (float)reader.GetReal("Shape", "shaft", 1.0);
	headLo = (float)reader.GetReal("Shape", "headMin", 1.0);
	headHi = (std::max)(headLo, (float)reader.GetReal("Shape", "headMax", headLo));
	shapeOn = reader.GetBoolean("Shape", "enabled", false) && chainNames.size() >= 2 && shaftScale >= 0.5f &&
		shaftScale <= 1.5f && headLo >= 0.5f && headHi <= 2.0f;   // sane or nothing: a typo must not deform him
	Note("aim|shapecfg|" + std::to_string((int)shapeOn), shapeOn ? "[aim] shape on: shaft x%.2f, head x%.2f .. %.2f per man\n"
		: "[aim] shape off (shaft x%.2f, head x%.2f .. %.2f)\n", shaftScale, headLo, headHi);
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
		(int)mouths, (int)(throatF.size() + throatNeckF.size()), (int)(throatM.size() + throatNeckM.size()), (int)handNames.size(), (int)gripSides.size(), (int)params.requireScene,
		params.captureAngle * 57.29578f, params.keepAngle * 57.29578f, params.captureMiss, params.keepMiss,
		params.entryAngle * 57.29578f, params.reach, params.maxStretch);
}

void ResetAims()
{
	held.clear();
	depths.clear();
	std::lock_guard<std::mutex> l(busyLock);
	busy.clear();
	busyAt = 0;
}

void UpdateAims()
{
	depths.clear();
	// Rapport's MCM (RFAK): switches aim or shape off, and retunes the shape; none heard, the ini's
	FaceAuthority::Knobs knobs;
	const bool knobsHeard = FaceAuthority::CurrentKnobs(knobs);
	const bool aimOn = enabled && (!knobsHeard || (knobs.enabled & FaceAuthority::kKnobAim));
	const bool shapeNow = shapeOn && (!knobsHeard || (knobs.enabled & FaceAuthority::kKnobShape));
	const float shaftNow = knobsHeard ? knobs.shaftScale : shaftScale;
	const float headLoNow = knobsHeard ? knobs.headMin : headLo, headHiNow = knobsHeard ? knobs.headMax : headHi;
	if (!aimOn && !shapeNow && held.empty())
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
	if (aimOn) {
		for (auto& e : actorEntries) {
			Actor* a = e.actor;
			if (!a || !G::Root(a) || !G::Root(a))
				continue;
			bool inScene = inSceneNow(a->formID);
			AddAnatomyTargets(a, inScene, targets);
			AddMouthTarget(a, inScene, targets);
			if (!inScene && params.requireScene)
				continue;                             // a hand out of a scene holds nobody's shaft here
			AddGripTargets(a, inScene, targets);
			for (auto& n : handNames)
				if (NiAVObject* h = Find(G::Root(a), n))
					hands.push_back(AimSolve::Hand{ a->formID, ToV3(G::World(h).pos) });
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
		if (!aimOn && !shapeNow && found == held.end())
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
			const NiMatrix43& cur = G::Local(nodes[i]).rot;
			if (!(h.wrote && i < h.wroteRot.size() && SameRot(cur, h.wroteRot[i]))) {
				keyed = keyed || h.wrote;
				h.baseRot[i] = cur;
			}
		}
		for (size_t k = 1; k < n; k++) {
			const NiPoint3& cur = G::Local(nodes[k]).pos;
			if (!(h.wrote && k - 1 < h.wrotePos.size() && SamePos(cur, h.wrotePos[k - 1])))
				h.basePos[k - 1] = cur;
		}
		h.baseScale.resize(n, 1.0f);
		for (size_t i = 0; i < n; i++) {
			float cur = G::Local(nodes[i]).scale;
			if (!(h.wrote && i < h.wroteScale.size() && cur == h.wroteScale[i]))
				h.baseScale[i] = cur;
		}
		if (h.wrote && !h.toldKeyed) {
			Note("aim|keyed|" + std::to_string(a->formID) + (keyed ? "|1" : "|0"), keyed ?
				"[aim] %08X: the animation keys %s every frame - the correction is laid on its pose each frame\n" :
				"[aim] %08X: nothing keys %s between frames - the correction rides on the pose the animation last set\n",
				a->formID, chainNames.front().c_str());
			h.toldKeyed = true;
		}

		if (!aimOn && !shapeNow) {                    // switched off: put the animation's pose back, once
			if (h.wrote)
				PutBack(h, nodes);
			held.erase(a->formID);
			continue;
		}

		// the chain as the animation put it: locals, and offsets in world units
		const NiTransform& parent = G::World(G::Parent(nodes[0]));
		AimSolve::Chain c;
		c.owner = a->formID;
		c.inScene = inSceneNow(a->formID);
		c.parent = AimSolve::FromMatrix(Actual(parent.rot));
		c.root = WorldPoint(parent, G::Local(nodes[0]).pos);
		c.locals.resize(n);
		c.offsets.assign(n, V3{});
		// the animation's scales, not the shape's: the shape keeps every joint where the animation put it
		float scale = parent.scale * h.baseScale[0];
		for (size_t i = 0; i < n; i++) {
			const NiMatrix43& rot = i + 1 < n ? h.baseRot[i] : G::Local(nodes[i]).rot;
			c.locals[i] = AimSolve::FromMatrix(Actual(rot));
			if (i > 0) {
				c.offsets[i] = AimSolve::Scale(ToV3(h.basePos[i - 1]), scale);
				scale *= h.baseScale[i];
			}
		}

		if (probe && aimOn) {
			std::vector<V3> joints;
			std::vector<AimSolve::Quat> world;
			AimSolve::Pose(c, c.locals, 1.0f, joints, world);
			for (const AimSolve::Target& t : targets) {
				if (t.kind != AimSolve::kMouth || t.owner == a->formID || joints.empty())
					continue;
				V3 tip = joints.back();
				V3 rel = AimSolve::Sub(tip, t.point);
				float depth = AimSolve::Dot(rel, t.in) - mouthLead;     // past her lips (+) or short of them
				float aside = AimSolve::Length(AimSolve::Sub(rel, AimSolve::Scale(t.in, AimSolve::Dot(rel, t.in))));
				if (AimSolve::Length(rel) > 40.0f)
					continue;
				// every 2 s per pair, and whenever the verdict changes; no per-pair cap (the first probe's 120 went
				// on a kneeling blowjob and the owner's lying one never showed), 3000 lines in all
				AimSolve::Fit f = AimSolve::Judge(c, joints, t, params, h.state.locked && h.state.lockedOwner == t.owner &&
					h.state.lockedKind == AimSolve::kMouth);
				std::uint64_t pairKey = ((std::uint64_t)a->formID << 32) | t.owner;
				auto& seen = probed[pairKey];
				static const char* lastWhy = nullptr;
				static int total = 0;
				const char* why = f.ok ? "would lock" : f.why;
				bool changed = why != lastWhy;
				if (total >= 3000 || (!changed && seen.first && ms - seen.first < 2000))
					continue;
				lastWhy = why;
				seen.first = ms;
				int line = seen.second++;
				total++;
				float length = AimSolve::ChainLength(c);
				float entrance = AimSolve::Length(AimSolve::Sub(t.point, joints.front()));
				char key[64];
				_snprintf_s(key, sizeof(key), _TRUNCATE, "aim|probe|%08X|%08X|%d", a->formID, t.owner, line);
				Note(key, "[aim] probe %08X -> %08X's mouth: %s (miss %.1f, turn %.0f deg, entrance %.1f = %.2f x the shaft "
					"%.1f, reach %.2f); the tip %.1f %s her lips, %.1f off her mouth's axis; locked %d\n", a->formID, t.owner,
					why, f.miss, f.angle * 57.29578f, entrance, length > 0.0f ? entrance / length : 0.0f, length, params.reach,
					std::fabs(depth), depth >= 0.0f ? "past" : "short of", aside,
					(int)(h.state.locked && h.state.lockedOwner == t.owner));
			}
		}

		std::uint32_t wasOwner = h.state.lockedOwner;
		int wasKind = h.state.lockedKind;
		AimSolve::Result r;
		if (aimOn)
			r = AimSolve::Update(h.state, c, targets, hands, params, dt);
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
		if (r.active || shapeNow) {
			// the aim's turns (or the animation's own), its stretch, and the shape on top
			std::vector<float> scaleMul, offsetMul;
			if (shapeNow)
				AimSolve::ShapeFactors(n, shaftNow, AimSolve::HeadFor(a->formID, headLoNow, headHiNow), scaleMul, offsetMul);
			else
				AimSolve::ShapeFactors(n, 1.0f, 1.0f, scaleMul, offsetMul);
			float stretch = r.active ? r.stretch : 1.0f;
			h.wroteRot.resize(n - 1);
			h.wrotePos.resize(n - 1);
			h.wroteScale.resize(n);
			for (size_t i = 0; i + 1 < n; i++) {
				G::Local(nodes[i]).rot = r.active ? Stored(Mul(AimSolve::ToMatrix(r.local[i]), Actual(h.baseRot[i])))
					: h.baseRot[i];
				h.wroteRot[i] = G::Local(nodes[i]).rot;
			}
			for (size_t k = 1; k < n; k++) {
				G::Local(nodes[k]).pos = h.basePos[k - 1] * (stretch * offsetMul[k]);
				h.wrotePos[k - 1] = G::Local(nodes[k]).pos;
			}
			for (size_t i = 0; i < n; i++) {
				G::Local(nodes[i]).scale = h.baseScale[i] * scaleMul[i];
				h.wroteScale[i] = G::Local(nodes[i]).scale;
			}
			h.wrote = true;
			UpdateWorldFrom(nodes[0], parent);
			// the genital depth (A-33), from the tip as just written. It lives INSIDE the write: 5217f05 put
			// it between the write and its else, which then hung off this if - so PutBack undid the write the
			// same frame unless the shaft was locked in a vagina, anus or mouth: the shape showed only there,
			// every release snapped instead of fading, and a grip never bent the shaft (release review)
			if (r.locked && r.targetKind != AimSolve::kHand) {
				for (const AimSolve::Target& t : targets) {
					if (t.owner != r.targetOwner || t.kind != r.targetKind)
						continue;
					V3 tip = ToV3(G::World(nodes.back()).pos);
					float d = AimSolve::Dot(AimSolve::Sub(tip, t.point), t.in);
					if (t.kind == AimSolve::kMouth)
						d -= mouthLead;                   // its entrance sits mouthLead in front of her lips
					d = (std::max)(0.0f, d);
					depths[a->formID] = (std::max)(depths[a->formID], d);
					if (t.kind != AimSolve::kMouth)       // a mouth's own depth is the contact mouth's (Mouth.cpp)
						depths[t.owner] = (std::max)(depths[t.owner], d);
					break;
				}
			}
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
			Actor* a = G::LookupActor(it->first);
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

float AimDepth(unsigned int formID)
{
	auto it = depths.find(formID);
	return it != depths.end() ? it->second : 0.0f;
}

bool AimSeesScene(unsigned int formID)
{
	return InScene(formID, NowMs());
}

bool RegisterAimFuncs(RE::BSScript::IVirtualMachine* vm)
{
	vm->BindNativeMethod("AnatomyAim"sv, "SetBusy"sv, SetBusy, true);   // true: NoWait, as the classic flag
	return true;
}
