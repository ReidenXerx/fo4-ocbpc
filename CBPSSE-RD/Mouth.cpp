// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-23: the contact-driven mouth, written over the face's merged morphs (ocbp.ini [Mouth]).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Mouth.h"

#include "Game.h"
#include "Hook.h"
#include "ActorEntry.h"
#include "ActorUtils.h"
#include "CollisionHub.h"
#include "config.h"
#include "log.h"

#include "FaceAuthority.h"
#include "FaceCompose.h"
#include "LipFit.h"
#include "Glans.h"
#include "Eyes.h"
#include "Aim.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>

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

	// The lips round what is inside (A-32, LipFit.h): per sex, how each mouth morph moves the lips' inner
	// edges at lipXs across the mouth, in the head's units ([Mouth] lipXs, lip<F|M><id>, tools/lips.py).
	// With a table, the fit replaces jaw/funnel/lift: the old gap (femaleGap) was the outer lips' distance,
	// and at the inner edge Jaw Open 1.0 opens 1.91, so the jaw opened a third too little (the clipping).
	const int kLipIds[] = { 2, 22, 46, 21, 44, 11, 34, 20, 43, 12, 35, 8, 31, 7, 30 };   // 7/30: Lip Corner In, the hug's
	float lipXs[LipFit::kSamples] = { -1.2f, -0.8f, -0.4f, 0.0f, 0.4f, 0.8f, 1.2f };
	LipFit::Table lipTable[2];       // 0 female, 1 male; count 0 = none (the old jaw/funnel/lift)
	LipFit::Params lipParams;
	// ACROSS, in the same fit: each lip<sex><id> also carries how far that morph moves the rim's two ends (the
	// mouth's inner corners), and lipRim<sex> where they rest. Lip Corner Out (8, 31) is in the table for that:
	// it opens a corner, Jaw Open widens both, the funnels narrow them (the owner, 2026-09-25: "fit head of
	// penis IN HORIZONTAL AXIS"). FaceCompose only RAISES 8 and 31 (Rapport's smiles use them).
	// The lips follow the section quickly BOTH ways ([Mouth] lipOpenRate / lipCloseRate, 1/s). At the jaw's
	// closeRate (6) they opened for the head and had not closed back onto the shaft before the next stroke:
	// "like the same big width as head" (the owner's look, 2026-09-25).
	float lipOpenRate = 30.0f, lipCloseRate = 25.0f;
	// The glans (Glans.h): [Mouth] glans=<the tip bone that carries it>, glansProfile=along:radius,... in the
	// tip sphere's radius. Empty: the spheres as they are.
	std::string glansNode;
	std::vector<Glans::Step> glansProfile;

	// The rest of the face while the mouth is busy (the owner, 2026-09-24: "expressions on the face
	// instead of stony, cheeks, brows, nose"). Each term raises one morph toward
	//   atContact + atDepth x (how deep the tip is past the lips) + atStroke x (how fast it moves),
	// and only ever RAISES it (max with what the face already has), so the animation's own face is
	// never erased. On a face Rapport holds (A-27) it rises above Rapport's ([Face] react, FaceCompose.h).
	// [Mouth] face=id:contact:depth:stroke,...
	struct FaceTerm { int id; float atContact, atDepth, atStroke; };
	const int kMaxFace = 16;
	std::vector<FaceTerm> faceTerms;
	float faceDepth = 6.0f;          // units past the lips for the full depth share
	float faceStroke = 20.0f;        // units/s of in-and-out for the full stroke share
	float faceRate = 6.0f;           // 1/s toward the new expression
	float strokeRate = 4.0f;         // 1/s: how fast the felt stroke speed follows the real one

	// Rapport's face authority (A-27, FaceAuthority.h), ocbp.ini [Face]:
	//   authority=1  Rapport's faces are written here (0: no listener, no hello, Rapport keeps its own way)
	//   react=1      the face while the mouth is busy (A-26) rises above a held face too (FaceCompose.h)
	//   probe=0      1: log what each line's lip sync moves, on any face in reach (a research aid, off for players)
	//   test=<hex>   the self-test: this plugin sends itself, through F4SE, the messages Rapport would
	//                (FaceAuthority::TestFace for that actor), held 20 s and released 10 s, over and over
	// The hello tells Rapport authority and react as they were at startup; a [Tuning] reload changes
	// what this plugin does but not what Rapport was told (a tester's path only).
	bool authority = true;
	bool react = true;
	bool probe = false;
	std::uint32_t testConfigured = 0;           // [Face] test as read; latched into testForm on a load

	// ---- the engine: Address Library ids (Hook.h), 1.10.163 addresses in the comments ----
	// AE 1.11.240: the only function in either executable reading [p+i*4+0x1C8] and [p+i*4+0x18] and [p+0xF0]
	// (tools/rd/opscan.py) is the merge, 0x6D1630 (final = maxss(+0xF0, +0x1C8), unrolled); same prologue (lock at
	// +0x2B4), flag in r8b, dt in xmm1, bool in al. Its one caller: 0x6E8F00. The lip-sync readers match too (OG
	// 0x667E50/0x668040/0x668170 = AE 0x6D0880/0x6D0C20/0x6D0D40: [p+0x2C0], [lip+0xC] & 0x70000000), so every face
	// offset here is AE's as well. Ids from f4rd-runtime.bin and the AE Address Library agree; NG 1.10.984 known too.
	constexpr Hook::IdPair kMergeOwner{ 317245, 2209487 };   // 0x686040 / AE 0x6E8F00: the merge's one caller
	constexpr Hook::IdPair kMergeTarget{ 3139, 2209139 };    // 0x6689D0 / AE 0x6D1630: the face merge
	uintptr_t faceDataVtable = 0;                      // BSFaceGenAnimationData's vtable (1.10.163 0x2CE9C58), by RD id
	// the merge's first bytes as the classic build read them on 1.10.163 (Steam and GOG alike): checked there only
	const unsigned char kMergePrologue[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x78, 0x20,
	                                         0x41, 0x56, 0x48, 0x83, 0xEC, 0x60 };
	const int kWeights = 0x18;                  // float[54]: what the face mesh is built from
	const int kMfg = 0xF0;                      // float[54]: MFG, and the lip sync of a line being spoken
	const int kLipSync = 0x2C0;                 // the lip-sync object of the line being spoken, or null
	const int kMorphs = 54;
	const int kExpressionMorphs = 50;           // ids 0-49 are the named expression table
	const int kLeftUpperEyeLidDown = 18;        // the blink: never ours to hold
	const int kRightUpperEyeLidDown = 41;       // (the mouth's own ids are FaceCompose's)
	bool hooked = false;

	struct Override
	{
		void* data;
		UInt32 formID;
		FaceCompose::Mouth mouth;               // the contact mouth's share (all 0 on a face it is not at)
		bool held;                              // Rapport holds this face (face authority)
		FaceAuthority::Face rapport;            // and this is the face it holds
	};
	std::mutex publishLock;   // the face update may run on another thread than UpdateActors
	std::vector<Override> published;
	// Per face we write over (FaceCompose::Ledger, under publishLock): the engine's own weights from its
	// last merge, so the engine never reads ours back
	FaceCompose::Ledger<char> ledger;
	// [Face] probe: EVERY face merged since the last frame, held or not, with the engine's lip state and
	// MFG layer as its merge left them (under publishLock; emptied each frame)
	struct Probe
	{
		bool speaking = false;
		float mfg[kMorphs] = {};
	};
	std::unordered_map<const void*, Probe> heard;

	struct State
	{
		float inside = 0.0f, jaw = 0.0f, floor = 0.0f, funnel = 0.0f, lift = 0.0f;
		float sinceContact = 1e9f;
		unsigned frame = 0;
		float depth = 0.0f, lastDepth = -1.0f, stroke = 0.0f;
		float gdepth = 0.0f;                    // a shaft's depth in her vagina/anus, or his own anywhere (Aim)
		float face[kMaxFace] = {};
		float lip[LipFit::kMaxMorphs] = {};     // A-32: the fitted lips as they show (eased toward the fit)
	};
	std::unordered_map<UInt32, State> states;
	unsigned frameCount = 0;
	LARGE_INTEGER lastTick = {};

	// What lip sync moves ([Face] probe; Rapport asked, 2026-09-24, to narrow its MOUTH set): while the
	// engine plays a line on any face in reach, held by Rapport or not, its MFG layer (+0xF0, where the
	// lip sync goes) is watched, and when the line ends the log names the ids that MOVED (the lip sync)
	// apart from the ones that only HELD a value. A few lines per actor are enough.
	struct Speech
	{
		bool on = false;
		bool held = false;                          // Rapport held the face when the line began
		unsigned seen = 0;                          // the frame it was last watched
		float seconds = 0.0f;
		float lo[kMorphs] = {}, hi[kMorphs] = {};
		int lines = 0;
	};
	std::unordered_map<UInt32, Speech> speech;
	const int kSpeechLines = 8;

	// F4SE's messaging, for Rapport's messages and the hello (set at PostLoad, before anything is sent)
	const F4SE::MessagingInterface* messaging = nullptr;
	const char* const kSelf = "OCBPC plugin";   // F4SEPlugin_Query's name: what Rapport listens to
	const char* const kRapport = "Rapport";

	std::atomic<bool> testOn{ false };          // the self-test runs (from the first load on)
	std::atomic<bool> testRestart{ false };
	std::atomic<std::uint32_t> testForm{ 0 };   // its actor, latched when it starts: a reload cannot move it
	float testClock = 0.0f;
	bool testHeld = false;

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
		if (AnatomyLogSeen(key))
			return;                               // said once already: not even formatted again
		char line[512];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
		va_end(args);
		AnatomyLogLine(key, line);
	}

	// The actor's face data: MiddleProcess data + 0x3C8, only if it is what the engine says it is.
	// CommonLib: AIProcess (Actor +0x300) -> MiddleHighProcessData (+0x08) -> faceAnimationData (+0x3C8), the classic
	// middleProcess->unk08->unk3B0[3]. The vtable test proves the chain on whatever runtime this is: a wrong offset
	// cannot land on an object whose first word is BSFaceGenAnimationData's vtable.
	void* FaceData(Actor* actor)
	{
		auto* process = actor->currentProcess;
		if (!process || !process->middleHigh || !faceDataVtable)
			return nullptr;
		void* data = process->middleHigh->faceAnimationData;
		if (!data || *reinterpret_cast<uintptr_t*>(data) != faceDataVtable)
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

	// A line is playing: the engine's own test (+0x667E86, +0x668302), state 3 or 4 of the lip-sync
	// object. Read on the merge's thread right after it: the end-of-line release (+0x668040, the merge's
	// last call) nulls the pointer before it lets the object go.
	bool LipPlaying(void* data)
	{
		const char* lip = *reinterpret_cast<const char* const*>(reinterpret_cast<const char*>(data) + kLipSync);
		if (!lip)
			return false;
		std::uint32_t state = (*reinterpret_cast<const std::uint32_t*>(lip + 0xC) >> 28) & 7;
		return state == 3 || state == 4;
	}

	bool HookMerge(void* data, float dt, bool flag)
	{
		float* w = reinterpret_cast<float*>(reinterpret_cast<char*>(data) + kWeights);
		Override o;
		bool found = false;
		FaceCompose::Engine last;
		{
			std::lock_guard<std::mutex> guard(publishLock);
			for (auto& p : published) {
				if (p.data == data) {
					o = p;
					found = true;
					break;
				}
			}
			last = ledger.Before(data, found, found ? o.formID : 0);
		}
		FaceCompose::BeforeMerge(w, last);        // the engine reads its own last face back, never ours
		bool changed = origMerge(data, dt, flag);
		const bool listen = probe;
		bool speaking = (found || listen) && LipPlaying(data);
		if (listen) {
			Probe p;
			p.speaking = speaking;
			std::memcpy(p.mfg, reinterpret_cast<char*>(data) + kMfg, sizeof(p.mfg));
			std::lock_guard<std::mutex> guard(publishLock);
			heard[data] = p;
		}
		if (!found)
			return changed || last.has;           // a face just let go is rebuilt as the engine's
		FaceCompose::Ledger<char>::Entry now;
		now.formID = o.formID;
		// Rapport's face, then the contact mouth, then A-26 (FaceCompose.h)
		FaceCompose::AfterMerge(w, now.engine, o.held ? &o.rapport : nullptr, speaking, o.mouth, react);
		{
			std::lock_guard<std::mutex> guard(publishLock);
			ledger.After(data, now);
		}
		return true;                              // the mesh is rebuilt from what we wrote
	}

	struct Point { NiPoint3 pos; float r; };
	struct Chain { Actor* owner; bool prop; std::string name; std::vector<Point> pts; };

	// cellChanged: the faces of the cell just left may be gone, so the ledger keeps only this list
	void Publish(std::vector<Override>&& next, bool cellChanged = false)
	{
		std::vector<const void*> faces;
		faces.reserve(next.size());
		for (auto& o : next)
			faces.push_back(o.data);
		std::lock_guard<std::mutex> guard(publishLock);
		if (cellChanged)
			ledger.Keep(faces);
		else
			ledger.Published(faces);
		published.swap(next);
	}

	// A held face on an actor the mouth has nothing to do with: the mouth's share is all 0, so its writes
	// leave Rapport's values as they are
	Override HeldOnly(void* data, UInt32 formID, const FaceAuthority::Face& face)
	{
		Override o{ data, formID, {}, true, face };
		o.mouth.lidMax = EyeLidMax(formID);
		return o;
	}

	// A held actor's face data, found by its form: for the actors OCBPC's scan does not reach (another
	// cell, beyond its distance). Null unless it has 3D and a middle-high process with a face.
	void* HeldFaceData(UInt32 formID)
	{
		Actor* a = G::LookupActor(formID);
		if (!a || G::Deleted(a) || !G::Root(a) || !G::Root(a))
			return nullptr;
		return FaceData(a);
	}

	int OwnedCount(const FaceAuthority::Face& face)
	{
		int n = 0;
		for (int i = 0; i < kMorphs; i++)
			n += (face.owned >> i) & 1u;
		return n;
	}

	void Applied(UInt32 formID, const char* how)
	{
		char key[48];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|applied|%08X", formID);
		Note(key, "[face] %08X: the held face is on (%s)\n", formID, how);
	}

	std::string IdList(const std::vector<int>& ids)
	{
		std::string s;
		for (int id : ids)
			s += (s.empty() ? "" : " ") + std::to_string(id);
		return s.empty() ? "none" : s;
	}

	void EndSpeech(UInt32 formID, Speech& s)
	{
		s.on = false;
		if (++s.lines > kSpeechLines)
			return;
		std::vector<int> moved, steady;
		for (int i = 0; i < kMorphs; i++) {
			if (s.hi[i] - s.lo[i] > 0.1f)
				moved.push_back(i);
			else if (s.hi[i] > 0.05f)
				steady.push_back(i);
		}
		char key[48];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|speech|%08X|%d", formID, s.lines);
		// in a scene, AAF writes expressions to the same MFG layer: a line outside one is the clean sample
		Note(key, "[face] %08X (%s, %s) spoke a line for %.1f s: its lip sync moved %s; held steady %s (MFG layer)\n",
			formID, s.held ? "held by Rapport" : "its own face", AimSeesScene(formID) ? "in a scene" : "outside scenes",
			s.seconds, IdList(moved).c_str(), IdList(steady).c_str());
	}

	// speaking and mfg are what the engine's last merge of this face left (HookMerge keeps them)
	void WatchSpeech(UInt32 formID, bool held, bool speaking, const float* mfg, float dt)
	{
		if (!speaking) {
			auto it = speech.find(formID);
			if (it != speech.end() && it->second.on)
				EndSpeech(formID, it->second);
			return;
		}
		Speech& s = speech[formID];
		s.seen = frameCount;
		if (!s.on) {
			s.on = true;
			s.held = held;
			s.seconds = 0.0f;
			for (int i = 0; i < kMorphs; i++)
				s.lo[i] = s.hi[i] = mfg[i];
			return;
		}
		s.seconds += dt;
		for (int i = 0; i < kMorphs; i++) {
			s.lo[i] = (std::min)(s.lo[i], mfg[i]);
			s.hi[i] = (std::max)(s.hi[i], mfg[i]);
		}
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
	faceDepth = (std::max)(0.5f, (float)reader.GetReal("Mouth", "faceDepth", faceDepth));
	faceStroke = (std::max)(1.0f, (float)reader.GetReal("Mouth", "faceStroke", faceStroke));
	faceRate = (std::max)(0.5f, (float)reader.GetReal("Mouth", "faceRate", faceRate));
	strokeRate = (std::max)(0.5f, (float)reader.GetReal("Mouth", "strokeRate", strokeRate));
	// A-32's lip table: all of it or none of it (a partial table would fit the lips with half the morphs)
	{
		auto xs = Split(reader.Get("Mouth", "lipXs", ""), ',');
		bool xsOk = xs.size() == (size_t)LipFit::kSamples;
		for (int k = 0; xsOk && k < LipFit::kSamples; k++)
			lipXs[k] = (float)std::atof(xs[k].c_str());
		const char* sexes[2] = { "F", "M" };
		for (int s = 0; s < 2; s++) {
			LipFit::Table t;
			bool ok = xsOk;
			bool corners = true;                             // every row with its corners' moves: the hug
			for (int id : kLipIds) {
				auto halves = Split(reader.Get("Mouth", std::string("lip") + sexes[s] + std::to_string(id), ""), ';');
				if (halves.size() != 3 && halves.size() != 4) {   // upper; lower; left,right[; corner left,right]
					ok = false;
					break;
				}
				auto up = Split(halves[0], ','), lo = Split(halves[1], ','), ends = Split(halves[2], ',');
				if (up.size() != (size_t)LipFit::kSamples || lo.size() != (size_t)LipFit::kSamples || ends.size() != 2) {
					ok = false;
					break;
				}
				int m = t.count++;
				t.id[m] = id;
				for (int k = 0; k < LipFit::kSamples; k++) {
					t.up[m][k] = (float)std::atof(up[k].c_str());
					t.lo[m][k] = (float)std::atof(lo[k].c_str());
				}
				t.left[m] = (float)std::atof(ends[0].c_str());
				t.right[m] = (float)std::atof(ends[1].c_str());
				auto cs = halves.size() == 4 ? Split(halves[3], ',') : std::vector<std::string>();
				if (cs.size() == 2) {
					t.cornerL[m] = (float)std::atof(cs[0].c_str());
					t.cornerR[m] = (float)std::atof(cs[1].c_str());
				}
				else
					corners = false;
			}
			auto corner = Split(reader.Get("Mouth", std::string("lipCorner") + sexes[s], ""), ',');
			if (ok && corners && corner.size() == 2) {
				t.restCornerL = (float)std::atof(corner[0].c_str());
				t.restCornerR = (float)std::atof(corner[1].c_str());
			}
			auto rim = Split(reader.Get("Mouth", std::string("lipRim") + sexes[s], ""), ',');
			if (ok && rim.size() == 2) {
				t.restLeft = (float)std::atof(rim[0].c_str());
				t.restRight = (float)std::atof(rim[1].c_str());
			}
			lipTable[s] = ok ? t : LipFit::Table{};
		}
		lipParams.clearance = (float)reader.GetReal("Mouth", "lipClearance", lipParams.clearance);
		lipParams.hug = (std::max)(0.0f, (float)reader.GetReal("Mouth", "lipHug", lipParams.hug));
		lipParams.cornerInside = (std::max)(0.0f, (float)reader.GetReal("Mouth", "lipCornerInside", lipParams.cornerInside));
		lipOpenRate = (std::max)(1.0f, (float)reader.GetReal("Mouth", "lipOpenRate", lipOpenRate));
		lipCloseRate = (std::max)(1.0f, (float)reader.GetReal("Mouth", "lipCloseRate", lipCloseRate));
		Note("mouth|lips|" + std::to_string(lipTable[0].count) + "|" + std::to_string(lipTable[1].count),
			lipTable[0].count || lipTable[1].count ? "[mouth] lips fitted round what is inside: %d morphs (women), %d (men)\n"
			: "[mouth] no lip table ([Mouth] lip*): the jaw opens by femaleGap/maleGap as before (%d/%d)\n",
			lipTable[0].count, lipTable[1].count);
	}
	// the glans: all of the profile or none of it
	glansNode = reader.Get("Mouth", "glans", "");
	glansProfile.clear();
	for (auto& step : Split(reader.Get("Mouth", "glansProfile", ""), ',')) {
		auto parts = Split(step, ':');
		if (parts.size() != 2) {
			glansProfile.clear();
			break;
		}
		glansProfile.push_back(Glans::Step{ (float)std::atof(parts[0].c_str()), (float)std::atof(parts[1].c_str()) });
	}
	if (!Glans::Valid(glansProfile) || glansNode.empty())
		glansProfile.clear();
	Note("mouth|glans|" + glansNode + "|" + std::to_string(glansProfile.size()), glansProfile.empty()
		? "[mouth] no glans profile ([Mouth] glans/glansProfile): the tip is its collider sphere\n"
		: "[mouth] the glans on %s: %d step(s), the crown where the mesh has it\n", glansNode.c_str(),
		(int)glansProfile.size());
	// face=id:contact:depth:stroke,... ids 0-49 of the expression table; never a mouth id (Rapport's
	// MOUTH set: the contact mouth's and a line's lip sync) nor the blink
	faceTerms.clear();
	int refused = 0;
	for (auto& term : Split(reader.Get("Mouth", "face", ""), ',')) {
		auto parts = Split(term, ':');
		if (parts.size() != 4) {
			refused++;
			continue;
		}
		FaceTerm ft{ atoi(parts[0].c_str()), (float)atof(parts[1].c_str()), (float)atof(parts[2].c_str()),
			(float)atof(parts[3].c_str()) };
		// the mouth (Rapport's MOUTH set, which a line's lip sync and the contact mouth drive) and the
		// blink are not the reaction's to raise: FaceCompose skips them too
		bool mouth = FaceAuthority::IsMouth(ft.id);
		bool blink = ft.id == kLeftUpperEyeLidDown || ft.id == kRightUpperEyeLidDown;
		if (ft.id < 0 || ft.id >= kExpressionMorphs || mouth || blink || (int)faceTerms.size() >= kMaxFace) {
			refused++;
			continue;
		}
		faceTerms.push_back(ft);
	}
	char key[48];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "mouth|face|%d|%d", (int)faceTerms.size(), refused);
	Note(key, "[mouth] face while busy: %d term(s)%s\n", (int)faceTerms.size(),
		refused ? " (some refused: bad form, out of range, a mouth id or the blink)" : "");
	if (enabled && chainNames.empty() && !useProps)
		enabled = false;                          // nothing could ever reach a mouth
}

// [Face] test=<form id>: hex, 1 to 8 digits, an optional 0x. Anything else is no test at all (strtoul
// would read "false" as 0xFA).
static std::uint32_t ParseForm(const std::string& text)
{
	std::string s = text;
	if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s = s.substr(2);
	if (s.empty() || s.size() > 8 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
		return 0;
	return (std::uint32_t)strtoul(s.c_str(), nullptr, 16);
}

void LoadFaceConfig(INIReader& reader)
{
	authority = reader.GetBoolean("Face", "authority", true);
	react = reader.GetBoolean("Face", "react", true);
	probe = reader.GetBoolean("Face", "probe", false);
	std::string test = reader.Get("Face", "test", "");
	testConfigured = ParseForm(test);
	if (!test.empty() && !testConfigured)
		Note("face|test|refused", "[face] [Face] test=%s is not a form id (hex): no self-test\n", test.c_str());
}

static void InstallMouthHookUnguarded()
{
	if (hooked)
		return;
	if (!enabled && !authority) {
		Note("mouth|off", "[mouth] off ([Mouth] enabled=0 or no chains) and [Face] authority=0: the merge is "
			"not hooked\n");
		return;
	}
	// never patch a build we have not proven: the merge's one call (Runtime Database: owner and target by Address
	// Library id, the unique direct call between them, the E8/rel32 check), the face data's vtable, and on 1.10.163
	// the merge's own prologue as the classic build read it
	const auto site = Hook::CallSite(kMergeOwner, kMergeTarget, "[mouth] the face merge's call");
	const auto merge = Hook::Resolve(kMergeTarget);
	const auto vtable = REL::IDDatabase::get().resolve(RE::VTABLE::BSFaceGenAnimationData[0]);
	if (!site || !merge || !vtable) {
		Note("mouth|build", "[mouth] this game build is not proven for the mouth (call %d, merge %d, face data vtable %d): "
			"the mouth and Rapport's faces stay off\n", (int)site.has_value(), (int)merge.has_value(), (int)(bool)vtable);
		return;
	}
	const unsigned char* code = reinterpret_cast<const unsigned char*>(*merge);
	bool prologue = !Hook::OgFamily() || std::memcmp(code, kMergePrologue, sizeof(kMergePrologue)) == 0;
	if (!prologue) {
		Note("mouth|build", "[mouth] 1.10.163, but the merge's prologue is not the one read: the mouth stays off\n");
		return;
	}
	faceDataVtable = REL::Module::get().base() + *vtable.rva;
	// The engine's one CALL to the merge is pointed at ours, and ours calls the merge as it is (its bytes are never
	// touched: a 2026-09-23 entry detour with DetourXS cut "sub rsp,60h" in half and killed every save load).
	origMerge = reinterpret_cast<MergeFn>(Hook::WriteCall(*site, reinterpret_cast<uintptr_t>(&HookMerge)));
	bool reaches = reinterpret_cast<uintptr_t>(origMerge) == *merge;
	hooked = reaches;
	Note("mouth|on", "[mouth] %s: mouth %s (%d chain(s), props %d, gap F %.2f M %.2f), [Face] authority %d, "
		"react %d, probe %d; the merge's call hooked, its code untouched\n",
		reaches ? "on" : "OFF, the hooked call did not return the merge", enabled ? "on" : "off", (int)chainNames.size(),
		(int)useProps, femaleGap, maleGap, (int)authority, (int)react, (int)probe);
}

// The mouth's install reads the game's code before it patches it, the way the eye hook's does, and runs
// under the same kind of guard (release review, 2026-09-26): an unguarded fault here would unload this
// plugin with the eye hook already pointing into it, and the game would die at the next eye update with
// no crash log - the failure 7ff0bde fixed on the eye path. Here a fault leaves the mouth off and says so.
static DWORD mouthFaultCode = 0;
static void* mouthFaultAt = nullptr;

static int MouthInstallFilter(EXCEPTION_POINTERS* e)
{
	mouthFaultCode = e->ExceptionRecord->ExceptionCode;
	mouthFaultAt = e->ExceptionRecord->ExceptionAddress;
	return EXCEPTION_EXECUTE_HANDLER;
}

static bool InstallMouthGuarded()
{
	__try {
		InstallMouthHookUnguarded();
		return true;
	}
	__except (MouthInstallFilter(GetExceptionInformation())) {
		return false;
	}
}

void InstallMouthHook()
{
	if (InstallMouthGuarded())
		return;
	enabled = false;                            // the mouth stays off; a call already patched runs the merge as it is
	authority = false;
	HMODULE self = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(&MouthInstallFilter), &self);
	uintptr_t at = reinterpret_cast<uintptr_t>(mouthFaultAt), mine = reinterpret_cast<uintptr_t>(self),
		game = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	Note("mouth|fault", "[mouth] FAULT while installing: code %08lX at %p (cbp.dll+%llX, Fallout4.exe+%llX); the mouth "
		"and Rapport's faces are off, the game goes on\n", (unsigned long)mouthFaultCode, mouthFaultAt,
		(unsigned long long)(at - mine), (unsigned long long)(at - game));
}

bool MouthOpening(Actor* actor, NiPoint3& centre, NiPoint3& outward, NiPoint3* up)
{
	if (!actor || !G::Root(actor) || !G::Root(actor))
		return false;
	BSFixedString headName("HEAD");
	NiAVObject* head = G::Root(actor)->GetObjectByName(headName);
	if (!head)
		return false;
	const NiTransform& t = G::World(head);
	NiMatrix43 toWorld = t.rot.Transpose();          // as UpdateMouths places it, below
	centre = t.pos + toWorld * ((actorUtils::IsActorMale(actor) ? maleMouth : femaleMouth) * t.scale);
	outward = Normalized(toWorld * facingBone);
	if (up)
		*up = Normalized(toWorld * upBone);
	return true;
}

void UpdateMouths()
{
	if (!hooked) {
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

	// the self-test ([Face] test): our own messages through F4SE, as Rapport's would come
	std::uint32_t test = testForm;
	if (testOn && test) {
		if (testRestart.exchange(false)) {
			testClock = 0.0f;
			testHeld = false;
		}
		testClock += dt;
		bool hold = std::fmod(testClock, 30.0f) < 20.0f;
		if (hold != testHeld) {
			testHeld = hold;
			bool delivered;
			if (hold) {
				FaceAuthority::SetMessage m = FaceAuthority::TestFace(test);
				delivered = messaging->Dispatch(FaceAuthority::kSet, &m, sizeof(m), kSelf);
			}
			else {
				FaceAuthority::ClearMessage m{ FaceAuthority::kVersion, test };   // never form 0: everyone
				delivered = messaging->Dispatch(FaceAuthority::kClear, &m, sizeof(m), kSelf);
			}
			if (!delivered)
				Note("face|test|lost", "[face] self-test: F4SE delivered the test message to no one\n");
		}
	}

	// Rapport's MCM (RFAK): each knob only switches off or retunes what the ini set up; none heard, the ini's
	FaceAuthority::Knobs knobs;
	const bool knobsHeard = FaceAuthority::CurrentKnobs(knobs);
	auto knobOn = [&](std::uint32_t bit) { return !knobsHeard || (knobs.enabled & bit) != 0; };
	const bool lipFitOn = knobOn(FaceAuthority::kKnobLipFit);
	const bool reactionOn = knobOn(FaceAuthority::kKnobReaction);
	const bool deepOn = knobOn(FaceAuthority::kKnobDeep);
	const float reactGain = knobsHeard ? knobs.reactScale : 1.0f;
	const float lipGain = knobsHeard ? knobs.lipSpeed : 1.0f;
	LipFit::Params lipNow = lipParams;
	if (knobsHeard)
		lipNow.clearance = knobs.lipClearance;

	// the faces Rapport holds: matched to the actors scanned below, and looked up by form for the rest
	std::vector<std::pair<std::uint32_t, FaceAuthority::Face>> held;
	const std::uint64_t clockMs = EyeClockMs();
	if (authority)
		held = FaceAuthority::Snapshot(clockMs);     // eased faces as they show now (bit 8)
	// the glances wearing a face (RFAX, bit 7), by looker
	std::unordered_map<UInt32, FaceAuthority::Running> glanceFaces;
	for (auto& g : FaceAuthority::Glances(clockMs))
		if (g.faceMask)
			glanceFaces[g.looker] = g;
	auto wearGlance = [&](Override& o) {
		auto g = glanceFaces.find(o.formID);
		if (g == glanceFaces.end())
			return;
		o.mouth.glanceMask = g->second.faceMask;
		std::memcpy(o.mouth.glanceFace, g->second.face, sizeof(o.mouth.glanceFace));
		o.mouth.glanceWeight = FaceAuthority::GlanceFaceWeight(clockMs, g->second.startMs, g->second.glance.durationMs);
	};
	std::unordered_map<UInt32, size_t> heldAt;
	std::vector<bool> heldDone(held.size(), false);
	for (size_t i = 0; i < held.size(); i++)
		heldAt[held[i].first] = i;

	// what could be in a mouth: every penis chain, base to tip, and (if on) every hand prop; nothing
	// while the mouth is off (a held face still applies)
	std::vector<Chain> chains;
	std::unordered_map<Actor*, std::unordered_map<std::string, const Collision*>> byActor;
	for (auto& c : otherColliders) {
		if (!enabled)
			break;
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
			bool tipFound = false;
			for (auto& n : names) {
				auto it = actorNodes.second.find(n);
				tipFound = it != actorNodes.second.end() && !it->second->collisionSpheres.empty();
				if (it != actorNodes.second.end())
					for (auto& s : it->second->collisionSpheres)
						ch.pts.push_back(Point{ s.worldPos, (float)s.radius });
			}
			if (tipFound && !glansProfile.empty() && names.back() == glansNode)
				Glans::Shape(ch.pts, glansProfile, skin);   // the crown behind the tip bone, at its width
			if (!ch.pts.empty())
				chains.push_back(ch);
		}
	}

	std::vector<Override> next;
	std::vector<std::pair<UInt32, const void*>> listened;   // [Face] probe: every face in reach, held or not
	BSFixedString headName("HEAD");
	for (auto& e : actorEntries) {
		Actor* a = e.actor;
		if (!a || !G::Root(a) || !G::Root(a))
			continue;
		void* data = FaceData(a);
		if (!data)
			continue;
		if (probe)
			listened.emplace_back(a->formID, data);
		NiAVObject* head = G::Root(a)->GetObjectByName(headName);
		if (!head)
			continue;
		bool male = actorUtils::IsActorMale(a);
		const NiTransform& t = G::World(head);
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

		float need = 0.0f, over = -1e9f, approach = 0.0f, depth = 0.0f;
		bool inside = false;
		const Chain* who = nullptr;
		struct Section { float qs, qu, hs, hu; };     // A-32: where each crossing cuts her lip plane
		std::vector<Section> sections;
		for (auto& ch : chains) {
			if (ch.owner == a && !ch.prop)
				continue;                         // her own penis bones are never in her mouth
			if (Length(ch.pts.back().pos - M) > 60.0f && Length(ch.pts.front().pos - M) > 60.0f)
				continue;
			bool crossed = false;
			auto consider = [&](const NiPoint3& X, float r, const NiPoint3& dir) {
				NiPoint3 q = X - M;
				float qs = Dot(q, S), qu = Dot(q, U);
				if (std::fabs(qs) > halfWidth || qu < -below || qu > above)
					return;
				float hu = SectionHalfU(dir, F, U, r);
				sections.push_back(Section{ qs, qu, SectionHalfU(dir, F, S, r), hu });
				inside = true;
				crossed = true;
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
			if (crossed && dT < 0.0f)
				depth = (std::max)(depth, -dT);       // how far the tip is past her lips (F points out)
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
			const LipFit::Table& lt = lipTable[male ? 1 : 0];
			if (lt.count > 0 && lipFitOn) {
				// A-32: what crosses her lips, in the head's own units (the table's), across the mouth; the
				// head's +x is S = F x U. Several crossings (two fingers): the lips go round all of them.
				LipFit::Want want;
				float sc = t.scale > 0.01f ? t.scale : 1.0f;
				want.lo = 1e9f;
				want.hi = -1e9f;
				for (const Section& sec : sections) {              // its extent across the mouth: the corners'
					want.lo = (std::min)(want.lo, (sec.qs - sec.hs) / sc);
					want.hi = (std::max)(want.hi, (sec.qs + sec.hs) / sc);
				}
				want.across = !sections.empty();
				for (int k = 0; k < LipFit::kSamples; k++) {
					for (const Section& sec : sections) {
						float dx = (lipXs[k] - sec.qs / sc) / (std::max)(sec.hs / sc, 0.05f);
						if (std::fabs(dx) >= 1.0f)
							continue;
						float h = sec.hu / sc * std::sqrt(1.0f - dx * dx);
						float top = sec.qu / sc + h, bottom = sec.qu / sc - h;
						want.top[k] = want.spans[k] ? (std::max)(want.top[k], top) : top;
						want.bottom[k] = want.spans[k] ? (std::min)(want.bottom[k], bottom) : bottom;
						want.spans[k] = true;
					}
				}
				float fit[LipFit::kMaxMorphs] = {};
				LipFit::Fit(lt, want, lipNow, st.lip, fit);
				for (int m = 0; m < lt.count; m++)
					st.lip[m] = Toward(st.lip[m], fit[m], (fit[m] > st.lip[m] ? lipOpenRate : lipCloseRate) * lipGain, dt);
				char lk[96];
				_snprintf_s(lk, sizeof(lk), _TRUNCATE, "mouth|lips|%08X", a->formID);
				Note(lk, "[mouth] %08X: lips round it: jaw %.2f, funnels %.2f/%.2f, upper lip up %.2f/%.2f down %.2f/%.2f, "
					"lower lip down %.2f/%.2f up %.2f/%.2f, corners out %.2f/%.2f in %.2f/%.2f (hug %s)\n", a->formID, fit[0], fit[1],
					fit[2], fit[3], fit[4], fit[7], fit[8], fit[5], fit[6], fit[9], fit[10], fit[11], fit[12], fit[13], fit[14],
					lt.restCornerL < 0.0f ? "on" : "off: no corners in the table");
			}
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
		// the rest of the face: how deep the tip is, and how fast it moves in and out
		float depthNow = inside ? depth : 0.0f;
		if (st.lastDepth >= 0.0f && dt > 0.0f)
			st.stroke = Toward(st.stroke, std::fabs(depthNow - st.lastDepth) / dt, strokeRate, dt);
		st.lastDepth = depthNow;
		st.depth = Toward(st.depth, depthNow, faceRate, dt);
		st.gdepth = Toward(st.gdepth, AimDepth(a->formID), faceRate, dt);
		int terms = (std::min)((int)faceTerms.size(), kMaxFace);
		for (int k = 0; k < terms; k++) {
			const FaceTerm& ft = faceTerms[k];
			float target = ft.atContact + ft.atDepth * Clamp(st.depth / faceDepth, 0.0f, 1.0f)
				+ ft.atStroke * Clamp(st.stroke / faceStroke, 0.0f, 1.0f);
			target = reactionOn ? target * reactGain : 0.0f;
			st.face[k] = Toward(st.face[k], Clamp(target, 0.0f, 1.0f), faceRate, dt);
		}
		auto h = heldAt.find(a->formID);
		const FaceAuthority::Face* rapport = h != heldAt.end() ? &held[h->second].second : nullptr;
		const float lidMax = EyeLidMax(a->formID);
		if (st.inside > 0.001f || st.floor > 0.001f || rapport || lidMax < 1.0f || glanceFaces.count(a->formID)) {
			Override o{ data, a->formID, {}, false, {} };
			o.mouth.lidMax = lidMax;
			wearGlance(o);
			o.mouth.inside = st.inside;
			// A-29: the same depth as A-26, or a shaft's in her vagina or anus, or his own in any opening
			o.mouth.deep = deepOn ? (std::max)(st.inside * Clamp(st.depth / faceDepth, 0.0f, 1.0f),
				Clamp(st.gdepth / faceDepth, 0.0f, 1.0f)) : 0.0f;
			o.mouth.jaw = st.jaw;
			o.mouth.floor = st.floor;
			o.mouth.funnel = st.funnel;
			o.mouth.lift = st.lift;
			const LipFit::Table& lt = lipTable[actorUtils::IsActorMale(a) ? 1 : 0];
			o.mouth.lipCount = lipFitOn ? (std::min)(lt.count, FaceCompose::kMaxTerms) : 0;   // A-32: they replace jaw/funnel/lift
			for (int k = 0; k < o.mouth.lipCount; k++) {
				o.mouth.lipId[k] = lt.id[k];
				o.mouth.lipValue[k] = st.lip[k];
			}
			o.mouth.termCount = terms;
			for (int k = 0; k < terms; k++) {
				o.mouth.termId[k] = faceTerms[k].id;
				o.mouth.termValue[k] = st.face[k];
			}
			if (rapport) {
				o.held = true;
				o.rapport = *rapport;
				heldDone[h->second] = true;
				Applied(a->formID, "found by OCBPC's scan");
			}
			next.push_back(o);
		}
	}
	// held faces the scan did not reach (another cell, beyond OCBPC's distance): looked up by form
	for (size_t i = 0; i < held.size(); i++) {
		if (heldDone[i])
			continue;
		UInt32 formID = held[i].first;
		void* data = HeldFaceData(formID);
		if (!data) {
			char key[48];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "face|absent|%08X", formID);
			Note(key, "[face] %08X: a face is held for an actor with no face to write yet (not loaded, or not "
				"in high process); it applies once there is one\n", formID);
			continue;
		}
		next.push_back(HeldOnly(data, formID, held[i].second));
		wearGlance(next.back());
		Applied(formID, "looked up by form");
		if (probe)
			listened.emplace_back(formID, data);
	}
	// the probe: each held face's line, as the engine's last merge of it left the lip state and MFG layer
	if (probe) {
		struct Heard { UInt32 formID; bool held; Probe probe; };
		std::vector<Heard> got;
		{
			std::lock_guard<std::mutex> guard(publishLock);
			for (auto& l : listened) {
				auto it = heard.find(l.second);
				if (it != heard.end())
					got.push_back(Heard{ l.first, heldAt.count(l.first) != 0, it->second });
			}
			heard.clear();                        // every merge fills it again
		}
		for (auto& g : got)
			WatchSpeech(g.formID, g.held, g.probe.speaking, g.probe.mfg, dt);
	}
	// a line whose actor was not seen this frame has ended too
	for (auto& s : speech)
		if (s.second.on && s.second.seen != frameCount)
			EndSpeech(s.first, s.second);
	UpdateEyeProbe(actorEntries, dt);              // [Eyes] probe / test (dev)
	// forget whoever left: a stale entry would steer a face that is not theirs any more
	for (auto it = states.begin(); it != states.end();)
		it = it->second.frame == frameCount ? std::next(it) : states.erase(it);
	Publish(std::move(next));
}

// Rapport's messages (and, in the self-test, ours): on Rapport's thread, a Papyrus one
static void FaceMessage(F4SE::MessagingInterface::Message* msg)
{
	if (!msg)
		return;
	const char* who = msg->sender ? msg->sender : "?";
	FaceAuthority::Decoded d = FaceAuthority::Decode(msg->type, msg->data, msg->dataLen);
	char key[96];
	if (d.refused) {
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|refused|%s|%s", who, d.refused);
		Note(key, "[face] %s: refused %s\n", who, d.refused);
		return;
	}
	if (d.command == FaceAuthority::Command::GlanceFace) {
		FaceAuthority::SetGlanceFace(d.formID, d.face.owned, d.face.value, EyeClockMs());
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|glanceface|%s|%08X", who, d.formID);
		Note(key, "[face] %s: %08X's next glance wears a face of %d morph(s)\n", who, d.formID, OwnedCount(d.face));
	}
	else if (d.command == FaceAuthority::Command::Set) {
		FaceAuthority::Set(d.formID, d.face, EyeClockMs());   // eased if already held (bit 8)
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|set|%s|%08X", who, d.formID);
		Note(key, "[face] %s holds %08X's face: %d morph(s)%s\n", who, d.formID, OwnedCount(d.face),
			hooked && authority ? "" : " (but [Face] authority is off or the merge is not hooked: nothing will show)");
	}
	else if (d.command == FaceAuthority::Command::Deep) {
		bool kept = FaceAuthority::SetDeep(d.formID, d.face.deepMask, d.face.deep);
		int n = 0;
		for (int i = 0; i < FaceAuthority::kMorphs; i++)
			n += (int)((d.face.deepMask >> i) & 1u);
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|deep|%s|%08X|%d", who, d.formID, n);
		Note(key, kept ? "[face] %s: %08X's deep face, %d morph(s), blends in with oral depth\n"
			: "[face] %s: a deep face for %08X, who is not held: dropped\n", who, d.formID, n);
	}
	else if (d.command == FaceAuthority::Command::Knobs) {
		FaceAuthority::SetKnobs(d.knobs);
		const FaceAuthority::Knobs& k = d.knobs;
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|knobs|%s|%X|%.3f|%.2f|%.2f|%.2f|%.2f|%.2f", who, k.enabled,
			k.lipClearance, k.lipSpeed, k.shaftScale, k.headMin, k.headMax, k.reactScale);
		Note(key, "[face] %s's knobs: aim %d, shape %d, lip fit %d, reaction %d, deep %d; lip clearance %.3f, lip "
			"speed x%.2f, shaft x%.2f, head x%.2f .. %.2f, reaction x%.2f\n", who,
			(int)((k.enabled & FaceAuthority::kKnobAim) != 0), (int)((k.enabled & FaceAuthority::kKnobShape) != 0),
			(int)((k.enabled & FaceAuthority::kKnobLipFit) != 0), (int)((k.enabled & FaceAuthority::kKnobReaction) != 0),
			(int)((k.enabled & FaceAuthority::kKnobDeep) != 0), k.lipClearance, k.lipSpeed, k.shaftScale, k.headMin,
			k.headMax, k.reactScale);
	}
	else if (d.command == FaceAuthority::Command::Glance) {
		FaceAuthority::SetGlance(d.formID, d.glance, EyeClockMs());
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|glance|%s|%08X|%08X", who, d.formID, d.glance.target);
		if (d.glance.target)
			Note(key, "[face] %s: %08X looks into %08X's eyes for %u ms, lids %.2f open%s\n", who, d.formID,
				d.glance.target, d.glance.durationMs, d.glance.lidsOpen, EyesTurn() ? "" : " (Rapport was not told glances work: [Eyes] glances=0, or no eye hook"
				")");
		else
			Note(key, "[face] %s: %08X stops looking\n", who, d.formID);
	}
	else if (d.command == FaceAuthority::Command::Clear) {
		FaceAuthority::Clear(d.formID, EyeClockMs());   // one face fades out over 250 ms; 0 (everyone) at once
		_snprintf_s(key, sizeof(key), _TRUNCATE, "face|clear|%s|%08X", who, d.formID);
		if (d.formID)
			Note(key, "[face] %s let go of %08X's face\n", who, d.formID);
		else
			Note(key, "[face] %s let go of every face\n", who);
	}
}

void ListenForFaces(const F4SE::MessagingInterface* m)
{
	messaging = m;
	if (!messaging)
		return;
	if (!authority) {
		Note("face|listen", "[face] [Face] authority=0: not listening to Rapport, and no hello\n");
		return;
	}
	// F4SE refuses a sender it has not loaded, which is why this waits for PostLoad
	bool rapport = messaging->RegisterListener(FaceMessage, kRapport);
	Note("face|listen", rapport ? "[face] Rapport is loaded: listening for the faces it holds\n"
		: "[face] Rapport is not loaded: no faces to hold\n");
	if (testConfigured && !messaging->RegisterListener(FaceMessage, kSelf))
		Note("face|test|listen", "[face] self-test: F4SE would not let this plugin listen to itself\n");
}

void SayFaceHello()
{
	// the hello promises Rapport that its faces are applied here: only with the authority on and the
	// merge hooked
	if (!messaging || !authority)
		return;
	if (!hooked) {
		Note("face|hello", "[face] no hello: the merge is not hooked, so Rapport keeps its own way\n");
		return;
	}
	FaceAuthority::HelloMessage hello{ FaceAuthority::kVersion, FaceAuthority::kFeatureSetClear |
		FaceAuthority::kFeatureEngineLines | (react ? FaceAuthority::kFeatureReaction : 0u) |
		FaceAuthority::kFeatureDepthBlend | FaceAuthority::kFeatureKnobs | FaceAuthority::kFeatureGenitalDepth |
		FaceAuthority::kFeatureGlanceFace | FaceAuthority::kFeatureEasedFaces | FaceAuthority::kFeatureEyeRoll |
		(EyesTurn() ? FaceAuthority::kFeatureGlances : 0u) };
	bool heard = messaging->Dispatch(FaceAuthority::kHello, &hello, sizeof(hello), kRapport);
	Note("face|hello", heard ? "[face] hello sent: Rapport's faces are applied here\n"
		: "[face] hello not heard: Rapport is not loaded, or is not listening to \"OCBPC plugin\"\n");
}

void ReleaseAllFaces(const char* why)
{
	FaceAuthority::Clear(0);
	{
		// the faces about to be unloaded: their addresses may be handed to other actors' faces
		std::lock_guard<std::mutex> guard(publishLock);
		published.clear();
		ledger.Clear();
		heard.clear();
	}
	std::string key = std::string("face|release|") + why;
	Note(key, "[face] every held face let go: %s (Rapport sends each again on its next change)\n", why);
}

void RefreshHeldFaces()
{
	// A cell change: OCBPC's scan skips this frame, so UpdateMouths does not run, and the faces of the
	// cell just left may be gone, their addresses free for others. Publish only the held faces, each
	// found again by its form; the contact mouths come back with the next scan.
	if (!hooked)
		return;
	std::vector<Override> next;
	if (authority)
		for (auto& h : FaceAuthority::Snapshot(EyeClockMs()))
			if (void* data = HeldFaceData(h.first))
				next.push_back(HeldOnly(data, h.first, h.second));
	Publish(std::move(next), true);   // and the ledger forgets every face not in this list
}

void StartFaceAuthorityTest()
{
	// latched here: a config reload later cannot move the test to another actor or make it clear everyone
	testForm = testConfigured;
	if (!testConfigured || !messaging || !hooked || !authority)
		return;
	testRestart = true;
	testOn = true;
	char key[48];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "face|test|%08X", testConfigured);
	Note(key, "[face] self-test on: %08X's test face held 20 s of every 30, by our own messages through F4SE\n",
		testConfigured);
}
