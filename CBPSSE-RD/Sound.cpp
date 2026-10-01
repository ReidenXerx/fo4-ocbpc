// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-30: engine-driven sex sounds (fo4-anatomy A-67).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Sound.h"

#include "ActorEntry.h"
#include "ActorUtils.h"
#include "Aim.h"
#include "CollisionHub.h"
#include "FaceAuthority.h"
#include "Hook.h"
#include "Mouth.h"
#include "TubeCollide.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <cmath>
#include <cctype>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace
{
	// ---- the audio calls (OG 1.10.163 / AE 1.11.240 Address Library ids, A-67) ----
	constexpr Hook::IdPair kAudioManager{ 679748, 2267093 };      // BSAudioManager* () : the singleton
	constexpr Hook::IdPair kBuildFromDescriptor{ 1419045, 2267105 };
	constexpr Hook::IdPair kPlay{ 384073, 2267042 };
	constexpr Hook::IdPair kStop{ 1340948, 2267045 };
	constexpr Hook::IdPair kSetVolume{ 422259, 2267057 };
	constexpr Hook::IdPair kSetFrequency{ 940583, 2267059 };        // by elimination: the other float setter
	constexpr Hook::IdPair kFollow{ 1179144, 2267066 };             // SetObjectToFollow(NiAVObject*)
	constexpr Hook::IdPair kGetByName{ 196484, 2267104 };           // (manager, handle*, EDID, distance, flags, extra)
	// ---- the mute: the SoundPlay branch's descriptor lookup inside the sound-event handler ----
	constexpr Hook::IdPair kEventHandler{ 936308, 2193468 };
	constexpr Hook::IdPair kResolveSoundPlay{ 1061737, 2193485 };

	struct Handle   // BSSoundHandle: {id, assumeSuccess, state}; a fresh one is {-1, 0, 0}
	{
		std::uint32_t id = 0xFFFFFFFF;
		std::uint8_t assumeSuccess = 0;
		std::int8_t state = 0;
		std::uint16_t pad = 0;
	};
	static_assert(sizeof(Handle) == 8);

	using ManagerFn = void* (*)();
	using BuildFn = bool (*)(void* manager, Handle* out, void* descriptor, float distance, std::uint32_t flags, void* extra);
	using HandleFn = bool (*)(Handle*);
	using HandleFloatFn = bool (*)(Handle*, float);
	using FollowFn = void (*)(Handle*, RE::NiAVObject*);
	using ResolveFn = void* (*)(RE::TESObjectREFR* ref, RE::BSFixedString* sound);
	using ByNameFn = bool (*)(void* manager, Handle* out, const char* edid, float distance, std::uint32_t flags, void* extra);

	ByNameFn getByName = nullptr;
	std::atomic<std::uint32_t> played{ 0 };
	ManagerFn getManager = nullptr;
	BuildFn build = nullptr;
	HandleFn play = nullptr, stop = nullptr;
	HandleFloatFn setVolume = nullptr, setFrequency = nullptr;
	FollowFn follow = nullptr;
	ResolveFn origResolve = nullptr;

	bool enabled = true;          // [Sound] enabled: install the mute hook at all
	int force = -1;               // [Sound] force: -1 Rapport decides, 0/1 a test without Rapport
	std::atomic<bool> overrideOn{ true };   // the owner 2026-10-01: everything on by default; Rapport's MCM or [Sound] force turn it off
	std::atomic<std::uint32_t> muted{ 0 };
	bool hooked = false;
	bool installed = false;       // Install ran (the ini's force is read before it: no "not hooked" note that early)
	bool canPlay = false;
	std::mutex sourceLock;
	std::string source = "on by default";   // who last set the override
	DWORD faultCode = 0;
	void* faultAt = nullptr;

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

	// A pack's "SoundPlay.<SNDR>" for an actor in a scene, while the override is on: no descriptor, so the handler
	// plays nothing. Everything else goes through untouched. The handler may run off the main thread: only the
	// override flag (atomic), the form's own bytes and Aim's scene list (under its lock) are read here.
	// The test's evidence (the owner, 2026-10-01: "u have all logs we need?"): one cbp.log line per distinct pack
	// sound muted, and per distinct one that played in a scene while the override was off (proof the hook is reached).
	void NoteSound(bool mutedIt, RE::TESObjectREFR* ref, RE::BSFixedString* sound)
	{
		const char* name = sound && sound->c_str() ? sound->c_str() : "?";
		std::string key = std::string(mutedIt ? "sound|muted|" : "sound|passed|") + name;
		if (AnatomyLogSeen(key))
			return;
		Note(key, mutedIt ? "[sound] muted %s for %08X (in a scene, override on)\n"
			: "[sound] %s for %08X played (in a scene: the override is off, or a voice kept because Rapport is not "
			"loaded)\n", name, ref->formID);
	}

	// Rapport loaded: it voices both partners, so the packs' voices go too. Without it (the owner 2026-10-01: "On, bodies
	// only") only a pack's BODY sounds are muted and its voices play: nothing would replace them.
	bool RapportHere()
	{
		static std::atomic<int> here{ -1 };
		int h = here.load(std::memory_order_relaxed);
		if (h < 0) {
			h = GetModuleHandleA("Rapport.dll") ? 1 : 0;
			here.store(h, std::memory_order_relaxed);
		}
		return h == 1;
	}

	// A pack's body sound by its SNDR's name (measured 2026-10-01 on 228 SNDRs of 9 packs: categories mix voices and
	// bodies, names do not): ALBodySlapping, BP70SoundOral/Penetrate, DR_Gray_Squish, DR_blowjob, ZOut4SFX_ThrustHigh...
	// A voice word wins (DR_anal_fuck is a gasp, BP70VoiceFemaleVanillaKiss a voice).
	bool IsBodySound(const char* name)
	{
		if (!name)
			return false;
		std::string n(name);
		for (auto& ch : n)
			ch = (char)std::tolower((unsigned char)ch);
		for (const char* v : { "voice", "moan", "orgasm", "breath", "gasp", "grunt", "scream", "yes", "please", "god",
			"cum", "fuck", "pussy", "kiss", "talk", "line" })
			if (n.find(v) != std::string::npos)
				return false;
		for (const char* b : { "slap", "spank", "squish", "squelch", "slurp", "suck", "oral", "blowjob", "thrust",
			"penetrat", "splash", "wet", "fap", "clap" })
			if (n.find(b) != std::string::npos)
				return true;
		return false;
	}

	void* HookResolve(RE::TESObjectREFR* ref, RE::BSFixedString* sound)
	{
		if (ref && *reinterpret_cast<const std::uint8_t*>(reinterpret_cast<const char*>(ref) + 0x1A) == 0x41 &&   // an actor
			AimSeesScene(ref->formID)) {                                                   // (the game's own test), in a scene
			if (overrideOn.load(std::memory_order_relaxed) &&
				(RapportHere() || IsBodySound(sound && sound->c_str() ? sound->c_str() : nullptr))) {
				muted.fetch_add(1, std::memory_order_relaxed);
				NoteSound(true, ref, sound);
				return nullptr;
			}
			NoteSound(false, ref, sound);
		}
		return origResolve(ref, sound);
	}

	void InstallUnguarded()
	{
		// the audio calls: resolve all or none (a half-resolved set plays nothing)
		const auto rMgr = Hook::Resolve(kAudioManager), rBuild = Hook::Resolve(kBuildFromDescriptor),
			rPlay = Hook::Resolve(kPlay), rStop = Hook::Resolve(kStop), rVol = Hook::Resolve(kSetVolume),
			rFreq = Hook::Resolve(kSetFrequency), rFollow = Hook::Resolve(kFollow), rByName = Hook::Resolve(kGetByName);
		if (rMgr && rBuild && rPlay && rStop && rVol && rFreq && rFollow && rByName) {
			getByName = reinterpret_cast<ByNameFn>(*rByName);
			getManager = reinterpret_cast<ManagerFn>(*rMgr);
			build = reinterpret_cast<BuildFn>(*rBuild);
			play = reinterpret_cast<HandleFn>(*rPlay);
			stop = reinterpret_cast<HandleFn>(*rStop);
			setVolume = reinterpret_cast<HandleFloatFn>(*rVol);
			setFrequency = reinterpret_cast<HandleFloatFn>(*rFreq);
			follow = reinterpret_cast<FollowFn>(*rFollow);
			canPlay = true;
		}
		Note("sound|calls", canPlay ? "[sound] the audio calls resolved: the engine can play its own sounds\n"
			: "[sound] this game build is not proven for the audio calls: the engine plays no sound of its own\n");
		if (!enabled) {
			Note("sound|off", "[sound] [Sound] enabled=0: the packs' sounds are never muted\n");
			return;
		}
		// two calls on both runtimes (measured 2026-10-01): the SoundPlay branch (the name straight from the event) and
		// the one that splits a longer tag (SoundPlayAt's name.position); both resolve a sound for this reference
		const auto sites = Hook::CallSites(kEventHandler, kResolveSoundPlay, "[sound] the SoundPlay descriptor's calls", 2);
		const auto target = Hook::Resolve(kResolveSoundPlay);
		if (sites.empty() || !target) {
			Note("sound|build", "[sound] this game build is not proven for the SoundPlay mute (calls %d, lookup %d): the "
				"packs' sounds play as they are\n", (int)sites.size(), (int)target.has_value());
			return;
		}
		origResolve = reinterpret_cast<ResolveFn>(*target);   // set before any call reaches the hook
		bool reaches = true;
		for (const auto site : sites)
			reaches = Hook::WriteCall(site, reinterpret_cast<uintptr_t>(&HookResolve)) == *target && reaches;
		hooked = reaches;
		Note("sound|on", hooked ? "[sound] the SoundPlay descriptor's 2 calls hooked (their code untouched): the packs' "
			"sounds are muted in scenes while the override is on (Rapport's MCM; off until Rapport says so)\n"
			: "[sound] a hooked call did not return the descriptor lookup: the mute may miss some sounds\n");
	}

	int Filter(EXCEPTION_POINTERS* e)
	{
		faultCode = e->ExceptionRecord->ExceptionCode;
		faultAt = e->ExceptionRecord->ExceptionAddress;
		return EXCEPTION_EXECUTE_HANDLER;
	}

	bool InstallGuarded()
	{
		__try {
			InstallUnguarded();
			return true;
		}
		__except (Filter(GetExceptionInformation())) {
			return false;
		}
	}
}

namespace Sound
{
	void LoadConfig(INIReader& reader)
	{
		enabled = reader.GetBoolean("Sound", "enabled", true);
		force = (int)reader.GetInteger("Sound", "force", -1);
		if (force == 0 || force == 1)
			SetOverride(force == 1, "[Sound] force");
	}

	void Install()
	{
		if (!InstallGuarded())
			Note("sound|fault", "[sound] a fault while installing (code %08X at %p) was caught: no sound work this "
				"session\n", faultCode, faultAt);
		installed = true;
	}

	void SetOverride(bool on, const char* who)
	{
		if (force == 0 || force == 1)
			on = force == 1;                           // a test's [Sound] force wins over Rapport
		bool was = overrideOn.exchange(on);
		{
			std::lock_guard<std::mutex> l(sourceLock);
			source = who ? who : "?";
		}
		std::string key = std::string("sound|override|") + (on ? "1|" : "0|") + (who ? who : "?");
		if (was != on || !AnatomyLogSeen(key))
			Note(key, "[sound] the override is %s (%s)%s\n", on ? "ON" : "off", who ? who : "?",
				on && installed && !hooked ? ", but the mute is not hooked: the packs' sounds still play" : "");
	}

	bool Override() { return overrideOn.load(); }
	bool CanPlay() { return canPlay; }
	bool Hooked() { return hooked; }
	std::string OverrideSource()
	{
		std::lock_guard<std::mutex> l(sourceLock);
		return source;
	}
	std::uint32_t MutedCount() { return muted.load(); }
	bool RapportLoaded() { return RapportHere(); }

	std::uint32_t PlayAt(RE::TESForm* sndr, RE::NiAVObject* node, float volume, float frequency)
	{
		if (!canPlay || !sndr || !node)
			return 0xFFFFFFFF;
		void* manager = getManager();
		if (!manager)
			return 0xFFFFFFFF;
		Handle h;
		// the descriptor is the form's BSISoundDescriptor base, right after TESForm (0x20), as Sound.Play passes it
		void* descriptor = reinterpret_cast<char*>(sndr) + 0x20;
		build(manager, &h, descriptor, 0.0f, 0x10, nullptr);
		if (h.id == 0xFFFFFFFF)
			return 0xFFFFFFFF;
		follow(&h, node);
		if (volume != 1.0f)
			setVolume(&h, volume);
		if (frequency != 1.0f)
			setFrequency(&h, frequency);
		return play(&h) ? h.id : 0xFFFFFFFF;
	}

	void Stop(std::uint32_t id)
	{
		if (!canPlay || id == 0xFFFFFFFF)
			return;
		Handle h;
		h.id = id;
		h.state = 1;
		stop(&h);
	}
}

// ---- the engine's own sounds (A-67; the owner 2026-10-01: "we need make working pipeline", tuned by his ear since).
// Played by EDID (Anatomy.esp's SNDRs) at the pelvis or head, following it; events (RFAE) tell Rapport the moments.
// Every event that begins (kind 1) ends (kind 4) with the SAME partner and flags, also when the actor leaves the scene,
// unloads, the contact turns into something else, or the override goes off (the 2026-10-01 microscope).
namespace
{
	constexpr float kEnter = 0.3f;          // depth past the entrance that counts as inside
	constexpr float kTurn = 0.2f;           // a fall this far below the stroke's deepest point ends the stroke
	constexpr float kMinStroke = 1.0f;      // a stroke shallower than this makes no sound (a jiggle, not a thrust)
	constexpr float kHardSpeed = 60.0f;     // units per second: above this a stroke is a hard impact (RFAE 3)
	constexpr ULONGLONG kMinGapMs = 120;    // two stroke sounds never closer than this
	constexpr ULONGLONG kThrustEventMs = 250;   // RFAE 2 (and 3) at most this often per track (Rapport's ask)
	constexpr ULONGLONG kOutMs = 400;       // empty this long = penetration ended
	constexpr ULONGLONG kMaxStrokeMs = 3000;   // a longer gap is a pause, not a stroke: its period reads 0
	constexpr ULONGLONG kVoiceMaxAgeMs = 1500; // a Rapport voice still queued after this is late: dropped
	const char* kSlap = "AnatomySoundSlap";
	const char* kSquelch = "AnatomySoundSquelch";
	const char* kThrust = "AnatomySoundThrust";
	const char* kSlurp = "AnatomySoundSlurp";   // a shaft enters her mouth
	const char* kSuck = "AnatomySoundSuck";     // each oral stroke: the closed mouth moving
	const char* kLick = "AnatomySoundLick";     // a tongue on her (cunnilingus, anilingus): on a beat while it is there
	const char* kFinger = "AnatomySoundFinger"; // fingers in or on her
	const char* kHandjob = "AnatomySoundHandjob";   // a hand stroking a shaft
	const char* kSiphon = "AnatomySoundSiphon"; // now and then on top: air slipping between lips and skin (the owner 10-01:
	                                            // most of the sound is the closed mouth, the slurp is the accident)

	enum class Kind { Genital, Mouth, Finger, Fist, Toy, Handjob };

	struct Track
	{
		bool inside = false;
		bool rising = false;
		float depth = 0.0f;
		float trough = 0.0f;
		float peak = 0.0f;
		ULONGLONG troughAt = 0;
		ULONGLONG lastStroke = 0;
		ULONGLONG lastThrustEvent = 0;
		ULONGLONG emptySince = 0;
		UInt32 formID = 0;      // what began: the end repeats it
		UInt32 partner = 0;
		UInt32 flags = 0;
		Kind kind = Kind::Genital;
		std::uint32_t seen = 0; // the frame it was last stepped (a track not stepped is ended and dropped)
	};
	std::unordered_map<std::uint64_t, Track> tracks;   // (formID, channel): the scan thread's own
	std::uint32_t frame = 0;
	std::uint32_t rng = 0x9E3779B9u;
	std::atomic<bool> resetWanted{ false };

	bool Chance(std::uint32_t inN)   // true about once in inN calls
	{
		rng = rng * 1664525u + 1013904223u;
		return ((rng >> 8) % inN) == 0;
	}

	float Jitter()   // 0.95 .. 1.05: the same clip never plays at quite the same pitch twice
	{
		rng = rng * 1664525u + 1013904223u;
		return 0.95f + 0.1f * (float)((rng >> 8) & 0xFFFF) / 65535.0f;
	}

	bool PlayByName(const char* edid, RE::NiAVObject* node, float volume, float frequency)
	{
		if (!canPlay || !node)
			return false;
		void* manager = getManager();
		if (!manager)
			return false;
		Handle h;
		getByName(manager, &h, edid, 0.0f, 0x10, nullptr);
		if (h.id == 0xFFFFFFFF) {
			Note(std::string("sound|noedid|") + edid, "[sound] the game has no sound %s (is Anatomy.esp active and "
				"current?): nothing plays for it\n", edid);
			return false;
		}
		follow(&h, node);
		setVolume(&h, volume);
		setFrequency(&h, frequency);
		if (!play(&h))
			return false;
		played.fetch_add(1, std::memory_order_relaxed);
		Note(std::string("sound|played|") + edid, "[sound] the engine played %s (volume %.2f, pitch %.2f)\n", edid,
			volume, frequency);
		return true;
	}

	void SendEvent(UInt32 formID, UInt32 partner, UInt32 kind, float depth, float speed, UInt32 strokeMs,
		UInt32 flags)
	{
		auto* messaging = F4SE::GetMessagingInterface();
		if (!messaging)
			return;
		FaceAuthority::SoundEventMessage m{ 3, formID, partner, kind, depth, speed, strokeMs, flags };
		messaging->Dispatch(FaceAuthority::kSoundEvent, &m, sizeof(m), nullptr);   // to everyone, Rapport keeps ours
	}

	RE::NiAVObject* PelvisOf(Actor* a)
	{
		RE::NiAVObject* root = G::Root(a);
		if (!root)
			return nullptr;
		static RE::BSFixedString pelvis("Pelvis");
		RE::NiAVObject* n = root->GetObjectByName(pelvis);
		return n ? n : root;
	}

	RE::NiAVObject* HeadOf(Actor* a)
	{
		RE::NiAVObject* root = G::Root(a);
		if (!root)
			return nullptr;
		static RE::BSFixedString head("Head");   // node names match case-insensitively (the string pool)
		RE::NiAVObject* n = root->GetObjectByName(head);
		return n ? n : root;
	}

	// RFAP (Rapport's voices): queued on Rapport's thread, played on the scan thread, which owns actorEntries
	struct VoiceRequest
	{
		UInt32 formID;
		UInt32 soundFormID;
		float volume;
		UInt32 flags;
		ULONGLONG at;
	};
	std::mutex voiceLock;
	std::vector<VoiceRequest> voiceQueue;
	std::unordered_map<UInt32, std::uint32_t> lastVoice;   // actor -> its last RFAP handle id (the scan thread's)
	std::atomic<std::uint32_t> voiced{ 0 };

	void PlayVoices(ULONGLONG now)
	{
		std::vector<VoiceRequest> pending;
		{
			std::lock_guard<std::mutex> l(voiceLock);
			pending.swap(voiceQueue);
		}
		for (const VoiceRequest& r : pending) {
			char key[64];
			if (now - r.at > kVoiceMaxAgeMs) {   // the scan did not run for a while (a loading screen): too late now
				Note("sound|voice|late", "[sound] a Rapport voice waited over 1.5 s to play (the engine's frame was not "
					"running): dropped, the first time\n");
				continue;
			}
			Actor* a = nullptr;
			for (auto& e : actorEntries)
				if (e.actor && e.actor->formID == r.formID) {
					a = e.actor;
					break;
				}
			if (!a) {
				_snprintf_s(key, sizeof(key), _TRUNCATE, "sound|voice|noactor|%08X", r.formID);
				Note(key, "[sound] Rapport asked a voice for %08X, which the engine does not track (not loaded or "
					"not a body it simulates): nothing plays\n", r.formID);
				continue;
			}
			RE::TESForm* sndr = RE::TESForm::GetFormByID(r.soundFormID);
			if (!sndr || sndr->GetFormType() != RE::ENUM_FORM_ID::kSNDR) {
				_snprintf_s(key, sizeof(key), _TRUNCATE, "sound|voice|nosndr|%08X", r.soundFormID);
				Note(key, "[sound] Rapport asked for %08X, which is not a sound descriptor (SNDR): nothing plays\n",
					r.soundFormID);
				continue;
			}
			if (r.flags & 1u) {
				auto it = lastVoice.find(r.formID);
				if (it != lastVoice.end())
					Sound::Stop(it->second);
			}
			const std::uint32_t id = Sound::PlayAt(sndr, HeadOf(a), r.volume);
			if (id == 0xFFFFFFFF) {
				_snprintf_s(key, sizeof(key), _TRUNCATE, "sound|voice|failed|%08X", r.soundFormID);
				Note(key, "[sound] Rapport's %08X for %08X did not play (the audio manager refused it)\n",
					r.soundFormID, r.formID);
				continue;
			}
			lastVoice[r.formID] = id;
			voiced.fetch_add(1, std::memory_order_relaxed);
			_snprintf_s(key, sizeof(key), _TRUNCATE, "sound|voice|played|%08X", r.soundFormID);
			Note(key, "[sound] Rapport's voice %08X played at %08X's head (volume %.2f%s)\n", r.soundFormID, r.formID,
				r.volume, (r.flags & 1u) ? ", the previous one cut" : "");
		}
	}

	struct Channel
	{
		UInt32 formID;
		UInt32 partner;
		float depth;      // this frame's
		UInt32 flags;     // RFAE's
		bool sounds;      // this channel plays the body sounds
		bool mouth;       // at the head (slurp, suck) rather than the pelvis
		Kind kind = Kind::Genital;
	};

	void End(Track& t)
	{
		if (t.inside)
			SendEvent(t.formID, t.partner, 4, 0.0f, 0.0f, 0, t.flags);
		t.inside = false;
		t.rising = false;
		t.emptySince = 0;
	}

	void Begin(Track& t, const Channel& c, Actor* a, ULONGLONG now, bool sound)
	{
		const float d = c.depth;
		t.inside = true;
		t.rising = true;
		t.trough = d;
		t.peak = d;               // a stale peak from the last time would fire a phantom thrust at once
		t.troughAt = now;
		t.emptySince = 0;
		t.lastStroke = 0;
		t.lastThrustEvent = 0;
		t.formID = c.formID;
		t.partner = c.partner;
		t.flags = c.flags;
		t.kind = c.kind;
		if (sound && c.sounds) {
			if (c.mouth)
				PlayByName(kSlurp, HeadOf(a), 1.0f, Jitter());
			else if (c.kind == Kind::Genital || c.kind == Kind::Toy || c.kind == Kind::Fist)
				PlayByName(kSquelch, PelvisOf(a), 1.0f, Jitter());   // fingers and a hand on a shaft slide in quietly
		}
		SendEvent(c.formID, c.partner, 1, d, 0.0f, 0, c.flags);
	}

	// one channel's strokes: a stroke rises from its shallowest point and ends when the depth falls kTurn below its
	// deepest; the deepest point is the thrust
	void Step(Track& t, const Channel& c, Actor* a, ULONGLONG now, float deep)
	{
		t.seen = frame;
		const float d = c.depth;
		if (!t.inside) {
			if (d > kEnter)
				Begin(t, c, a, now, true);
			t.depth = d;
			return;
		}
		if (d <= 0.05f) {       // empty: ended once it stays so
			if (!t.emptySince)
				t.emptySince = now;
			else if (now - t.emptySince > kOutMs)
				End(t);
			t.depth = d;
			return;
		}
		t.emptySince = 0;
		if (c.partner != t.partner || c.kind != t.kind || c.flags != t.flags) {   // someone or something else now
			End(t);
			Begin(t, c, a, now, false);
			t.depth = d;
			return;
		}
		if (d > t.depth) {
			if (!t.rising) {    // a new stroke starts at the shallowest point
				t.rising = true;
				t.trough = t.depth;
				t.troughAt = now;
			}
			t.peak = d;
		}
		else if (t.rising && d < t.peak - kTurn) {   // the deepest point just passed: the thrust
			t.rising = false;
			const float stroke = t.peak - t.trough;
			const float minStroke = (c.kind == Kind::Finger || c.kind == Kind::Handjob) ? 0.5f : kMinStroke;   // a hand moves less
			if (stroke >= minStroke && now - t.lastStroke >= kMinGapMs) {
				// the stroke period: deepest point to deepest point (Rapport fits a moan's length inside it);
				// 0 for the first stroke, or after a pause longer than any real stroke
				const ULONGLONG gap = now - t.lastStroke;
				const UInt32 strokeMs = (t.lastStroke && gap <= kMaxStrokeMs) ? (UInt32)gap : 0;
				t.lastStroke = now;
				const float secs = (std::max)(0.05f, (float)(now - t.troughAt) / 1000.0f);
				const float speed = stroke / secs;
				const float volume = (std::min)(1.0f, 0.75f + speed / 120.0f);
				if (c.sounds) {
					if (c.mouth) {
						RE::NiAVObject* head = HeadOf(a);
						PlayByName(kSuck, head, volume, Jitter());
						if (Chance(speed > 30.0f ? 3 : 6))   // a fast mouth lets air in more often
							PlayByName(kSiphon, head, volume * 0.9f, Jitter());
					}
					else {
						RE::NiAVObject* pelvis = PelvisOf(a);
						switch (c.kind) {
						case Kind::Finger: PlayByName(kFinger, pelvis, volume, Jitter()); break;
						case Kind::Handjob: PlayByName(kHandjob, pelvis, volume, Jitter()); break;
						case Kind::Fist:
						case Kind::Toy: PlayByName(kThrust, pelvis, volume, Jitter()); break;   // no hips meet: no slap
						default:
							PlayByName(kSlap, pelvis, volume, Jitter());
							PlayByName(kThrust, pelvis, volume * 0.9f, Jitter());
						}
					}
				}
				const UInt32 flags = c.flags | (t.peak >= deep ? FaceAuthority::kSoundEventDeep : 0u);
				if (now - t.lastThrustEvent >= kThrustEventMs) {   // the stroke and, when hard, its impact: one gate
					t.lastThrustEvent = now;
					SendEvent(c.formID, c.partner, 2, t.peak, speed, strokeMs, flags);
					if (speed > kHardSpeed)
						SendEvent(c.formID, c.partner, 3, t.peak, speed, strokeMs, flags);
				}
			}
		}
		t.depth = d;
	}

	// ---- contacts without a shaft (the owner 10-01): a mouth licking her, fingers or a fist in or on her, a toy in her, a
	// hand on a shaft. Measured against Aim's openings (entrance + inward axis): depth = along the axis, aside = off it.
	constexpr float kLickAside = 5.0f, kLickNear = -5.0f, kLickFar = 2.0f;   // the lips in front of / at her opening
	constexpr float kHandAside = 3.0f, kInside = 0.3f;                         // a fingertip or a toy past the entrance
	constexpr float kRubAside = 4.0f, kRubNear = -2.5f;                        // a fingertip on her, not in
	constexpr float kFistDepth = 5.0f, kFistHold = 3.5f;   // fingers this deep: a fist; it stays one until shallower than hold
	constexpr float kTipReach = 0.8f;   // the fingertip: past the last joint by this share of the last bone

	void Axis(const AimOpening& o, const NiPoint3& p, float& depth, float& aside)
	{
		NiPoint3 v = p - o.point;
		depth = v.x * o.in.x + v.y * o.in.y + v.z * o.in.z;
		NiPoint3 off = v - o.in * depth;
		aside = std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z);
	}

	// this frame's fingertips of every actor in a scene: found once, not per opening
	struct Hands
	{
		UInt32 formID;
		int count = 0;
		NiPoint3 tip[4];
	};
	std::vector<Hands> hands;

	void FindHands(const std::vector<Actor*>& inScene)
	{
		static RE::BSFixedString joints[4][2] = {   // index and middle finger, both hands: the last two joints
			{ RE::BSFixedString("LArm_Finger22"), RE::BSFixedString("LArm_Finger23") },
			{ RE::BSFixedString("LArm_Finger32"), RE::BSFixedString("LArm_Finger33") },
			{ RE::BSFixedString("RArm_Finger22"), RE::BSFixedString("RArm_Finger23") },
			{ RE::BSFixedString("RArm_Finger32"), RE::BSFixedString("RArm_Finger33") } };
		hands.clear();
		for (Actor* a : inScene) {
			RE::NiAVObject* root = G::Root(a);
			if (!root)
				continue;
			Hands h{ a->formID };
			for (auto& j : joints) {
				RE::NiAVObject* mid = root->GetObjectByName(j[0]);
				RE::NiAVObject* last = root->GetObjectByName(j[1]);
				if (!mid || !last)
					continue;
				const NiPoint3 p2 = G::World(mid).pos, p3 = G::World(last).pos;
				h.tip[h.count++] = p3 + (p3 - p2) * kTipReach;
			}
			if (h.count)
				hands.push_back(h);
		}
	}

	enum class Touch { None, Lick, Rub, Finger, Fist, Toy };
	struct Contact
	{
		Touch touch = Touch::None;
		float depth = 0.0f;
		UInt32 partner = 0;
	};

	// a surface contact (licking, rubbing) has no stroke to measure: it sounds on a beat while it lasts
	struct Beat
	{
		Touch touch = Touch::None;
		ULONGLONG next = 0;
		ULONGLONG last = 0;
		ULONGLONG gone = 0;
		UInt32 formID = 0;      // what began: the end repeats it
		UInt32 partner = 0;
		UInt32 flags = 0;
		std::uint32_t seen = 0;
	};
	std::unordered_map<std::uint64_t, Beat> beats;
	std::vector<PropLine> props;

	UInt32 TouchFlag(Touch t)
	{
		switch (t) {
		case Touch::Lick: return FaceAuthority::kSoundEventLick;
		case Touch::Rub:
		case Touch::Finger:
		case Touch::Fist: return FaceAuthority::kSoundEventHand;
		case Touch::Toy: return FaceAuthority::kSoundEventToy;
		default: return 0;
		}
	}

	void EndBeat(Beat& b)
	{
		if (b.touch != Touch::None) {
			SendEvent(b.formID, b.partner, 4, 0.0f, 0.0f, 0, b.flags);
			if (b.touch == Touch::Lick && b.partner && b.partner != b.formID)   // the licker's own: his mouth is free
				SendEvent(b.partner, b.formID, 4, 0.0f, 0.0f, 0, FaceAuthority::kSoundEventLick |
					(b.flags & FaceAuthority::kSoundEventAnal));
		}
		b = Beat{};
	}

	void Beats(std::uint64_t key, Actor* a, const AimOpening& o, const Contact& c, ULONGLONG now)
	{
		Beat& b = beats[key];
		b.seen = frame;
		const bool surface = c.touch == Touch::Lick || c.touch == Touch::Rub;
		if (b.touch != Touch::None && (!surface || b.touch != c.touch || b.partner != c.partner)) {
			if (!b.gone)
				b.gone = now;
			if (now - b.gone > kOutMs || surface) {   // gone long enough, or already something else on her
				EndBeat(b);
				b.seen = frame;
			}
			if (!surface)
				return;
		}
		if (!surface)
			return;
		b.gone = 0;
		if (b.touch == Touch::None) {
			b.touch = c.touch;
			b.formID = a->formID;
			b.partner = c.partner;
			b.flags = FaceAuthority::kSoundEventReceiver | (o.kind == 1 ? FaceAuthority::kSoundEventAnal : 0u) |
				(c.partner == a->formID ? FaceAuthority::kSoundEventSelf : 0u) | TouchFlag(c.touch);
			b.next = now;
			SendEvent(a->formID, c.partner, 1, 0.0f, 0.0f, 0, b.flags);
			if (c.touch == Touch::Lick && c.partner && c.partner != a->formID)   // the licker's own: his mouth is busy
				SendEvent(c.partner, a->formID, 1, 0.0f, 0.0f, 0, FaceAuthority::kSoundEventLick |
					(o.kind == 1 ? FaceAuthority::kSoundEventAnal : 0u));
		}
		if (now < b.next)
			return;
		const UInt32 gap = b.last ? (UInt32)(now - b.last) : 0;
		b.last = now;
		const bool lick = c.touch == Touch::Lick;
		b.next = now + (lick ? 450 : 380) + (ULONGLONG)(std::max)(0.0f, (Jitter() - 0.95f) * 4000.0f);   // lick 450-850, rub 380-780 ms
		PlayByName(lick ? kLick : kFinger, PelvisOf(a), lick ? 0.9f : 0.7f, Jitter());
		SendEvent(a->formID, c.partner, 2, 0.0f, 0.0f, gap <= kMaxStrokeMs ? gap : 0, b.flags);
	}

	// the best contact on one of her openings this frame: a toy, then fingers inside (a fist when deep), then a mouth on
	// it, then fingers on it. wasFist: this opening's track is a fist now (it stays one down to kFistHold)
	Contact Find(Actor* her, const AimOpening& o, bool wasFist)
	{
		Contact best;
		for (auto& line : props) {
			for (auto& p : line.pts) {
				float d, aside;
				Axis(o, p, d, aside);
				if (aside < kHandAside && d > kInside && d > best.depth)
					best = Contact{ Touch::Toy, d, line.owner };
			}
		}
		if (best.touch == Touch::Toy)
			return best;
		Contact rub;
		for (auto& h : hands) {
			for (int k = 0; k < h.count; k++) {
				float d, aside;
				Axis(o, h.tip[k], d, aside);
				if (aside < kHandAside && d > kInside && d > best.depth)
					best = Contact{ (d > kFistDepth || (wasFist && d > kFistHold)) ? Touch::Fist : Touch::Finger, d, h.formID };
				else if (aside < kRubAside && d > kRubNear && d <= kInside && rub.touch == Touch::None)
					rub = Contact{ Touch::Rub, 0.0f, h.formID };
			}
		}
		if (best.touch != Touch::None)
			return best;
		for (auto& m : AimOpenings()) {
			if (m.kind != 2 || m.owner == her->formID || !m.inScene)
				continue;
			float d, aside;
			Axis(o, m.point, d, aside);
			if (aside < kLickAside && d > kLickNear && d < kLickFar)
				return Contact{ Touch::Lick, 0.0f, m.owner };
		}
		return rub;
	}

	// everything open is closed with its end event (the override went off): Rapport never keeps a begun moment
	void CloseAll()
	{
		for (auto& [key, t] : tracks)
			End(t);
		for (auto& [key, b] : beats)
			EndBeat(b);
		tracks.clear();
		beats.clear();
	}
}

namespace Sound
{
	std::uint32_t PlayedCount() { return played.load(); }
	std::uint32_t VoicedCount() { return voiced.load(); }

	void QueueVoice(UInt32 formID, UInt32 soundFormID, float volume, UInt32 flags)
	{
		if (!std::isfinite(volume))
			return;
		volume = (std::min)(2.0f, (std::max)(0.0f, volume));
		std::lock_guard<std::mutex> l(voiceLock);
		if (voiceQueue.size() >= 64)   // a runaway sender cannot grow it without bound: the oldest goes
			voiceQueue.erase(voiceQueue.begin());
		voiceQueue.push_back({ formID, soundFormID, volume, flags, GetTickCount64() });
	}

	void Reset()
	{
		resetWanted.store(true);   // a save is loading: done on the scan thread's next frame
	}

	void Update()
	{
		const ULONGLONG now = GetTickCount64();
		if (resetWanted.exchange(false)) {   // the last game's moments and handles mean nothing in this one
			tracks.clear();
			beats.clear();
			lastVoice.clear();
			std::lock_guard<std::mutex> l(voiceLock);
			voiceQueue.clear();
		}
		if (canPlay)
			PlayVoices(now);   // override on or off: Rapport voices its actors either way
		if (!canPlay || !overrideOn.load(std::memory_order_relaxed)) {
			if (!tracks.empty() || !beats.empty())
				CloseAll();
			return;
		}
		frame++;
		const float deep = MouthDeepDepth();
		std::vector<Actor*> inScene;
		for (auto& e : actorEntries)
			if (e.actor && AimSeesScene(e.actor->formID))
				inScene.push_back(e.actor);
		for (Actor* a : inScene) {
			const UInt32 id = a->formID;
			const unsigned openings = AimReceivedKinds(id);   // bit 0 her vagina, bit 1 her anus holds a shaft
			const bool entered = openings != 0;
			const UInt32 partner = AimPartner(id);
			// channel 0, the genitals: the one entered (a shaft in the vagina or anus, any sex) plays the body sounds at
			// the pelvis; the shaft's owner's depth (vagina, anus or a mouth) only tells Rapport - it mirrors the
			// receiver's, whose channel plays the sound, so none doubles
			Channel genital{ id, partner, AimDepth(id),
				(entered ? FaceAuthority::kSoundEventReceiver : (AimInMouth(id) ? FaceAuthority::kSoundEventOral : 0u)) |
				((entered ? (openings & 2u) != 0 : AimDepthKind(id) == 1) ? FaceAuthority::kSoundEventAnal : 0u) |
				(partner == id ? FaceAuthority::kSoundEventSelf : 0u), entered, false };
			Step(tracks[(std::uint64_t)id << 3], genital, a, now, deep);
			// channel 1, the mouth: a shaft in this actor's mouth, sucking sounds at the head and oral events for
			// the mouth's owner (Rapport keeps a full mouth quiet)
			const UInt32 sucked = AimOralPartner(id);
			Channel mouth{ id, sucked, AimOralDepth(id), FaceAuthority::kSoundEventOral | FaceAuthority::kSoundEventReceiver |
				(sucked == id ? FaceAuthority::kSoundEventSelf : 0u), true, true };
			Step(tracks[((std::uint64_t)id << 3) | 1], mouth, a, now, deep);
			// channel 2, a hand on his shaft: its strokes, at his pelvis; he is the one stimulated (RECEIVER)
			const UInt32 hand = AimGripPartner(id);
			Channel handjob{ id, hand, AimGripDepth(id), FaceAuthority::kSoundEventHand | FaceAuthority::kSoundEventReceiver |
				(hand == id ? FaceAuthority::kSoundEventSelf : 0u), true, false, Kind::Handjob };
			Step(tracks[((std::uint64_t)id << 3) | 2], handjob, a, now, deep);
		}
		// channels 3 (vagina) and 4 (anus): what touches her without a shaft in that opening
		std::vector<const Actor*> live;
		for (auto& e : actorEntries)
			if (e.actor)
				live.push_back(e.actor);
		PropLines(props, live);
		FindHands(inScene);
		for (const AimOpening& o : AimOpenings()) {
			if (o.kind > 1 || !o.inScene)
				continue;
			Actor* her = nullptr;
			for (Actor* a : inScene)
				if (a->formID == o.owner)
					her = a;
			if (!her)
				continue;
			const bool shaft = (AimReceivedKinds(o.owner) & (1u << o.kind)) != 0;
			const std::uint64_t key = ((std::uint64_t)o.owner << 3) | (3 + o.kind);
			Track& t = tracks[key | 0x10000000000ull];
			Contact c = shaft ? Contact{} : Find(her, o, t.inside && t.kind == Kind::Fist);
			Beats(key, her, o, c, now);   // licking and rubbing
			const bool inside = c.touch == Touch::Finger || c.touch == Touch::Fist || c.touch == Touch::Toy;
			Channel in{ o.owner, c.partner, inside ? c.depth : 0.0f, FaceAuthority::kSoundEventReceiver |
				(o.kind == 1 ? FaceAuthority::kSoundEventAnal : 0u) | (inside ? TouchFlag(c.touch) : 0u) |
				(c.partner == o.owner ? FaceAuthority::kSoundEventSelf : 0u), true, false,
				c.touch == Touch::Toy ? Kind::Toy : c.touch == Touch::Fist ? Kind::Fist : Kind::Finger };
			if (inside || t.inside)
				Step(t, in, her, now, deep);
			else
				t.seen = frame;
		}
		// whatever was not stepped this frame (the actor left the scene, unloaded, her opening is gone): ended, dropped
		for (auto it = tracks.begin(); it != tracks.end();) {
			if (it->second.seen != frame) {
				End(it->second);
				it = tracks.erase(it);
			}
			else
				++it;
		}
		for (auto it = beats.begin(); it != beats.end();) {
			if (it->second.seen != frame) {
				EndBeat(it->second);
				it = beats.erase(it);
			}
			else
				++it;
		}
	}
}
