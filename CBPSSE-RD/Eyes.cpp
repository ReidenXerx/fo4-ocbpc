// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-25: glances, one actor looking into another's eyes (RFAG).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Eyes.h"

#include "Game.h"
#include "Hook.h"
#include "ActorEntry.h"
#include "ActorUtils.h"
#include "CollisionHub.h"
#include "FaceAuthority.h"
#include "Glance.h"
#include "Mouth.h"


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
	int test = 0;                               // 1: glance at the nearest actor; 2: the sweep; 3: look at me
	// look at me (test=3): every actor near the player looks into the player's eyes 4 s, then 4 s the engine's
	// own eyes, over and over, and the log names both uv beside where the player is. Where the two agree the
	// map is the engine's; where the eyes miss him in OUR 4 s, the owner names where they go instead
	const float kLookPhase = 4.0f;
	std::unordered_map<UInt32, int> lookLines;
	const int kLookLines = 150;                 // per actor
	// the sweep (test=2): every actor in reach, eyes to four fixed offsets in turn, 3 s each and 1 s centred
	// between, lids open, so the owner can name where each one points (the eyes' own axes, measured by eye)
	const float kSweep[4][2] = { { -0.12f, 0.0f }, { 0.0f, -0.06f }, { 0.12f, 0.0f }, { 0.0f, 0.06f } };
	std::mutex sweepLock;
	std::vector<UInt32> sweepers;               // the scan's, read by the eye update
	int sweepPose = -1;                          // -1: centred
	float eyeRise = 4.8f, eyeBack = 0.8f;
	float rollMax = 0.24f;                      // [Eyes] rollMax: how far up the eyes roll (kGlanceRoll)
	GlanceMath::Params params;
	const float kLidRate = 10.0f;               // 1/s: the lids open for a glance and give the face back
	const float kProbeEvery = 0.5f;             // s between probe lines per actor
	const int kProbeLines = 40;                 // per actor

	// ---- the engine: Address Library ids (Hook.h), 1.10.163 addresses in the comments (SAM's offsets there) ----
	// AE 1.11.240: 0x97E190, the function with the eye update's own shape (two [rdx+0x2C] = 0x7FFFFFFF render-pass
	// locks, 17 ucomiss against the material's +0xC/+0x10/+0x14 UVs, the geometry's +0x138 shader; frame 0x800 vs
	// 0x810), called from exactly two functions as in 1.10.163: 0xC33660 (a small wrapper) and 0xC339D0. The eye
	// offsets used here are the update's own on AE. Ids from f4rd-runtime.bin and the AE Address Library agree.
	constexpr Hook::IdPair kEyeTarget{ 1217611, 2219241 };                                        // 0x9C0410 / AE 0x97E190
	constexpr Hook::IdPair kEyeOwners[2] = { { 235979, 2228914 }, { 633524, 2228917 } };         // 0xD38AF0 / AE 0xC33660, 0xD38E60 / AE 0xC339D0
	// the prologue as read on 1.10.163: checked there only
	const unsigned char kEyePrologue[] = { 0x48, 0x8B, 0xC4, 0x55, 0x48, 0x8D, 0xA8, 0xE8, 0xF8, 0xFF, 0xFF,
	                                       0x48, 0x81, 0xEC, 0x10, 0x08, 0x00, 0x00 };
	typedef void (*EyeFn)(float dt);
	EyeFn origEyes = nullptr;
	bool hooked = false;
	// The first glance build faulted inside this plugin's Load (2026-09-25, f4se.log: "disabled, fatal
	// error occurred while loading plugin"), after the mouth's call was patched: the game died at the first
	// face. The install now runs under a guard that names the step it was on, and runs before the mouth's.
	const char* volatile installStep = "not started";
	DWORD faultCode = 0;
	void* faultAt = nullptr;

	// Runtime Database defines no BSShaderProperty or BSShaderMaterial: the classic code's view of them, by the offsets
	// measured on 1.10.163 (G::Measured, and textCoordOffset[2] at +0x0C of the material), proven per eye: the
	// geometry's shader property must carry BSLightingShaderProperty's vtable and its material the eye material's.
	uintptr_t shaderVtable = 0, eyeMaterialVtable = 0;   // resolved at install, by RD's VTABLE ids
	struct EyeMaterial
	{
		uintptr_t at = 0;
		float& F(size_t o) const { return *reinterpret_cast<float*>(at + o); }
		void GetOffsetUV(float* u, float* v) const
		{
			*u = F(0x0C);   // textCoordOffset[0].x
			*v = F(0x10);   // textCoordOffset[0].y
		}
		void SetOffsetUV(float u, float v) const
		{
			F(0x0C) = u;    // textCoordOffset[0], [1]: as the SDK's SetOffsetUV
			F(0x14) = u;
			F(0x10) = v;
			F(0x18) = v;
		}
	};
	struct RenderPassLock
	{
		uintptr_t at = 0;
		RenderPassLock& operator=(int32_t v)
		{
			*reinterpret_cast<int32_t*>(at) = v;
			return *this;
		}
	};
	struct EyeShader
	{
		EyeMaterial material;
		EyeMaterial* shaderMaterial = nullptr;
		RenderPassLock iLastRenderPassState;
	};
	using BSShaderProperty = EyeShader;
	EyeShader* ProvenShader(BSGeometry* geometry)
	{
		thread_local EyeShader ring[8];
		thread_local unsigned next = 0;
		const uintptr_t prop = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(geometry) + G::Measured::kGeometryShaderProperty);
		if (!prop || !shaderVtable || *reinterpret_cast<uintptr_t*>(prop) != shaderVtable)
			return nullptr;
		const uintptr_t mat = *reinterpret_cast<uintptr_t*>(prop + G::Measured::kShaderMaterial);
		if (!mat || !eyeMaterialVtable || *reinterpret_cast<uintptr_t*>(mat) != eyeMaterialVtable)
			return nullptr;
		EyeShader& s = ring[next++ % 8];
		s.material.at = mat;
		s.shaderMaterial = &s.material;
		s.iLastRenderPassState.at = prop + G::Measured::kShaderLastRenderPass;
		return &s;
	}

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

	Actor* ActorOf(UInt32 formID) { return G::LookupActor(formID); }

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
		if (BSGeometry* g = G::AsGeometry(o)) {
			const char* n = G::Name(o);
			std::string name = n ? n : "";
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return name.find("eyes") != std::string::npos ? g : nullptr;
		}
		NiNode* node = G::AsNode(o);
		if (!node)
			return nullptr;
		for (UInt16 i = 0; i < G::ChildCount(node); i++)
			if (BSGeometry* g = FindEyes(G::Child(node, i), depth + 1))
				return g;
		return nullptr;
	}

	// The eyes' shader property: by the NPC's eyes head part, as SAM finds it, else any geometry named
	// "...Eyes..." near the root (a templated NPC's own head parts may be empty)
	BSShaderProperty* EyeProperty(Actor* a)
	{
		if (!a || !G::Root(a) || !G::Root(a))
			return nullptr;
		NiAVObject* root = G::Root(a);
		// Runtime Database defines no BGSHeadPart (nor TESNPC's head parts): the classic build's own fallback, the
		// geometry named "...Eyes..." under the root, is the way here
		BSGeometry* geometry = FindEyes(root, 0);
		if (!geometry) {
			G::Once("eyes|none", "diag: {:08X}: no geometry named '...eyes...' within 4 levels of '{}'", a->formID, G::Name(root));
			return nullptr;
		}
		EyeShader* s = ProvenShader(geometry);
		if (!s) {
			const uintptr_t base = REL::Module::get().base();
			const uintptr_t prop = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(geometry) + G::Measured::kGeometryShaderProperty);
			const uintptr_t pv = prop ? *reinterpret_cast<uintptr_t*>(prop) : 0;
			const uintptr_t mat = prop ? *reinterpret_cast<uintptr_t*>(prop + G::Measured::kShaderMaterial) : 0;
			const uintptr_t mv = mat ? *reinterpret_cast<uintptr_t*>(mat) : 0;
			G::Once("eyes|shader", "diag: {:08X} '{}': shader vtable +{:X} (want +{:X}), material vtable +{:X} (want +{:X})",
				a->formID, G::Name(geometry), pv ? pv - base : 0, shaderVtable ? shaderVtable - base : 0, mv ? mv - base : 0,
				eyeMaterialVtable ? eyeMaterialVtable - base : 0);
		}
		return s;
	}

	// kept: actors whose lids something else holds open this frame (the sweep), which the glances must not let go
	void ApplyGlances(float dt, const std::unordered_set<UInt32>& kept)
	{
		auto running = FaceAuthority::Glances(EyeClockMs());
		std::unordered_set<UInt32> now = kept;
		for (auto& r : running) {
			Actor* a = ActorOf(r.looker);
			Actor* b = ActorOf(r.glance.target);
			float side = 0.0f, up = 0.0f, ahead = 0.0f;
			GlanceMath::UV want;
			const bool roll = (r.glance.flags & FaceAuthority::kGlanceRoll) != 0;
			bool reach = roll ? a != nullptr
				: a && b && Toward(a, b, side, up, ahead) && GlanceMath::Want(side, up, ahead, params, want);
			if (roll)
				want = GlanceMath::Roll(params, rollMax);
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

	// the sweep's actors, their lids held open (the owner, 2026-09-26: in a blowjob "eyes stopped working" -
	// her lids were shut by her face, and ApplyGlances let go of every look that was not a glance's)
	std::unordered_set<UInt32> ApplySweep(float dt)
	{
		std::unordered_set<UInt32> swept;
		std::vector<UInt32> who;
		int pose;
		{
			std::lock_guard<std::mutex> guard(sweepLock);
			who = sweepers;
			pose = sweepPose;
		}
		for (UInt32 id : who) {
			BSShaderProperty* property = EyeProperty(ActorOf(id));
			if (!property || !property->shaderMaterial)
				continue;
			GlanceMath::UV cur, want;
			if (pose >= 0) {
				want.x = kSweep[pose][0];
				want.y = kSweep[pose][1];
			}
			property->shaderMaterial->GetOffsetUV(&cur.x, &cur.y);
			GlanceMath::UV next = GlanceMath::Step(cur, want, params, dt);
			property->iLastRenderPassState = 0x7FFFFFFF;
			property->shaderMaterial->SetOffsetUV(next.x, next.y);
			std::lock_guard<std::mutex> guard(lookLock);
			Look& l = looks[id];
			l.active = true;
			l.lidsOpen = 1.0f;
			swept.insert(id);
		}
		return swept;
	}

	void HookEyes(float dt)
	{
		origEyes(dt);                              // the engine's own eyes first, every actor's
		dt = dt < 0.0f ? 0.0f : (dt > 0.1f ? 0.1f : dt);
		std::unordered_set<UInt32> kept;
		if (test == 2 && enabled && hooked)
			kept = ApplySweep(dt);
		ApplyGlances(dt, kept);
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
	params.signUp = reader.GetReal("Eyes", "signUp", -1.0) < 0.0 ? -1.0f : 1.0f;
	params.signSide = reader.GetReal("Eyes", "signSide", -1.0) < 0.0 ? -1.0f : 1.0f;
	// the vertical edge (the owner, 2026-09-26: "when up and bottom we can try push gaze more"): past the
	// engine's own 0.16 is ours to choose; 0.25 at most
	params.xMax = (float)reader.GetReal("Eyes", "uMax", params.xMax);
	params.xMax = params.xMax < 0.05f ? 0.05f : (params.xMax > 0.25f ? 0.25f : params.xMax);
	// v's edges: the up-and-down reach (the owner: "push gaze more up and more bottom")
	params.yMin = (float)reader.GetReal("Eyes", "vMin", params.yMin);
	params.yMin = params.yMin < -0.15f ? -0.15f : (params.yMin > -0.03f ? -0.03f : params.yMin);
	params.yMax = (float)reader.GetReal("Eyes", "vMax", params.yMax);
	params.yMax = params.yMax < 0.03f ? 0.03f : (params.yMax > 0.15f ? 0.15f : params.yMax);
	rollMax = (float)reader.GetReal("Eyes", "rollMax", rollMax);
	rollMax = rollMax < 0.05f ? 0.05f : (rollMax > 0.3f ? 0.3f : rollMax);
	eyeRise = (float)reader.GetReal("Eyes", "eyeRise", eyeRise);
	eyeBack = (float)reader.GetReal("Eyes", "eyeBack", eyeBack);
	probe = reader.GetBoolean("Eyes", "probe", false);
	test = (int)reader.GetInteger("Eyes", "test", 0);
	{
		auto axes = reader.Get("Eyes", "axes", "");
		float v[4] = {};
		if (!axes.empty() && sscanf_s(axes.c_str(), "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) == 4) {
			params.a = v[0];
			params.b = v[1];
			params.c = v[2];
			params.d = v[3];
		}
	}
	char key[96];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "eyes|config|%d|%d|%d|%d|%d|%d", (int)enabled, (int)glancesOn,
		(int)params.signUp, (int)params.signSide, (int)probe, (int)test);
	Note(key, "[eyes] [Eyes] enabled %d, glances told to Rapport %d, signs (only for axes=0,0,0,0) u = %+d x 0.25 up, v = %+d x 0.25 side, eyes %.1f up / "
		"%.1f back from the mouth, probe %d, test %d\n", (int)enabled, (int)glancesOn, (int)params.signUp, (int)params.signSide,
		eyeRise, eyeBack, (int)probe, test);
	if (params.a != 0.0f || params.b != 0.0f || params.c != 0.0f || params.d != 0.0f)
		Note("eyes|axes", "[eyes] axes: u = 0.25 (%+.2f up %+.2f side) to +-%.3f, v = 0.25 (%+.2f up %+.2f side) from %.3f to "
			"%.3f, rolls to %.3f\n", params.a, params.b, params.xMax, params.c, params.d, params.yMin, params.yMax, rollMax);
	if (test == 3)
		Note("eyes|lookme", "[eyes] look at me: every actor near the player looks into the player's eyes 4 s (ours), then "
			"4 s is the engine's own look, over and over\n");
	if (test == 2)
		Note("eyes|sweep", "[eyes] the sweep: every actor near the player turns its eyes to 1: u -0.12, 2: v -0.06, 3: u +0.12, "
			"4: v +0.06 - in that order, 3 s each, centred 1 s between, lids open, over and over\n");
}

static void InstallEyeHookUnguarded()
{
	if (hooked || !enabled)
		return;
	// never patch a build we have not proven: both calls to the eye update (Runtime Database + E8 check), the eye
	// shader's two vtables, and on 1.10.163 the update's prologue as read
	installStep = "resolving the eye update and its two calls";
	const auto fn = Hook::Resolve(kEyeTarget);
	const auto first = Hook::CallSite(kEyeOwners[0], kEyeTarget, "[eyes] the eye update's first call");
	const auto second = Hook::CallSite(kEyeOwners[1], kEyeTarget, "[eyes] the eye update's second call");
	const auto shader = REL::IDDatabase::get().resolve(RE::VTABLE::BSLightingShaderProperty[0]);
	const auto material = REL::IDDatabase::get().resolve(RE::VTABLE::BSLightingShaderMaterialEye[0]);
	installStep = "reading the eye update's first bytes";
	bool prologue = fn && (!Hook::OgFamily() ||
		std::memcmp(reinterpret_cast<const void*>(*fn), kEyePrologue, sizeof(kEyePrologue)) == 0);
	if (!fn || !first || !second || !shader || !material || !prologue) {
		Note("eyes|build", "[eyes] this game build is not proven for the eyes (update %d, calls %d/%d, vtables %d/%d, "
			"prologue %d): no glance turns an eye\n", (int)fn.has_value(), (int)first.has_value(), (int)second.has_value(),
			(int)(bool)shader, (int)(bool)material, (int)prologue);
		return;
	}
	shaderVtable = REL::Module::get().base() + *shader.rva;
	eyeMaterialVtable = REL::Module::get().base() + *material.rva;
	origEyes = reinterpret_cast<EyeFn>(*fn);
	installStep = "patching the first call";
	bool reaches = Hook::WriteCall(*first, reinterpret_cast<uintptr_t>(&HookEyes)) == *fn;
	installStep = "patching the second call";
	reaches = Hook::WriteCall(*second, reinterpret_cast<uintptr_t>(&HookEyes)) == *fn && reaches;
	hooked = reaches;
	installStep = "logging";
	Note("eyes|on", reaches ? "[eyes] the eye update's two calls hooked (its code untouched): glances turn eyes\n"
		: "[eyes] a hooked call did not return the eye update: glances may turn eyes only some frames\n");
}

static int InstallFilter(EXCEPTION_POINTERS* e)
{
	faultCode = e->ExceptionRecord->ExceptionCode;
	faultAt = e->ExceptionRecord->ExceptionAddress;
	return EXCEPTION_EXECUTE_HANDLER;
}

static bool InstallGuarded()
{
	__try {
		InstallEyeHookUnguarded();
		return true;
	}
	__except (InstallFilter(GetExceptionInformation())) {
		return false;
	}
}

void InstallEyeHook()
{
	if (InstallGuarded())
		return;
	enabled = false;                            // no eye is turned; a call already patched runs the engine's
	HMODULE self = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(&InstallFilter), &self);
	uintptr_t at = reinterpret_cast<uintptr_t>(faultAt), mine = reinterpret_cast<uintptr_t>(self),
		game = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	Note("eyes|fault", "[eyes] FAULT while %s: code %08lX at %p (cbp.dll+%llX, Fallout4.exe+%llX); glances are off, the "
		"game goes on\n", installStep, (unsigned long)faultCode, faultAt, (unsigned long long)(at - mine),
		(unsigned long long)(at - game));
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
	Actor* player = RE::PlayerCharacter::GetSingleton();
	testClock += dt;
	bool fire = test == 1 && testClock >= 4.0f;
	if (fire)
		testClock = 0.0f;
	static float lookTick = 0.0f;
	lookTick += dt;
	bool ours = std::fmod(testClock, 2.0f * kLookPhase) < kLookPhase;
	bool look = test == 3 && lookTick >= 1.0f;
	if (look)
		lookTick = 0.0f;
	if (test == 2) {
		float t = std::fmod(testClock, 16.0f);
		int pose = (int)(t / 4.0f);
		std::vector<UInt32> who;
		for (auto& e : actors)
			if (e.actor && e.actor != player)
				who.push_back(e.actor->formID);
		std::lock_guard<std::mutex> guard(sweepLock);
		sweepers = who;
		sweepPose = std::fmod(t, 4.0f) < 3.0f ? pose : -1;
	}
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
		if (look && player) {
			float side, up, ahead;
			GlanceMath::UV want, cur;
			BSShaderProperty* property = EyeProperty(a);
			bool sees = Toward(a, player, side, up, ahead);
			bool reach = sees && GlanceMath::Want(side, up, ahead, params, want);
			if (property && property->shaderMaterial)
				property->shaderMaterial->GetOffsetUV(&cur.x, &cur.y);
			if (ours && reach) {
				FaceAuthority::Glance g;
				g.target = player->formID;
				// ends with OUR phase, so the engine's 4 s are the engine's own (a 1.5 s glance renewed each
				// second wrote the first 1.5 s of them, and the log named our uv as the engine's)
				float left = kLookPhase - std::fmod(testClock, 2.0f * kLookPhase);
				g.durationMs = (std::uint32_t)((std::min)(1.5f, (std::max)(0.1f, left)) * 1000.0f);
				g.lidsOpen = 1.0f;
				FaceAuthority::SetGlance(a->formID, g, EyeClockMs());
			}
			int& n = lookLines[a->formID];
			if (sees && n < kLookLines) {
				char key[64];
				_snprintf_s(key, sizeof(key), _TRUNCATE, "eyes|look|%08X|%d", a->formID, n++);
				Note(key, "[eyes] look %08X, %s: eyes at uv (%.3f, %.3f); the player at side %.2f, up %.2f, ahead %.2f; "
					"ours %s (%.3f, %.3f)\n", a->formID, ours ? "OURS" : "the engine's", cur.x, cur.y, side, up, ahead,
					reach ? "wants" : "would not look (behind her)", want.x, want.y);
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
