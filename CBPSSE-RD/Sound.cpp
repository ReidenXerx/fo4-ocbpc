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

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <unordered_map>
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
	std::atomic<bool> overrideOn{ false };
	std::atomic<std::uint32_t> muted{ 0 };
	bool hooked = false;
	bool installed = false;       // Install ran (the ini's force is read before it: no "not hooked" note that early)
	bool canPlay = false;
	std::mutex sourceLock;
	std::string source = "none (no word from Rapport)";   // who last set the override
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
			: "[sound] %s for %08X played (in a scene, override off)\n", name, ref->formID);
	}

	void* HookResolve(RE::TESObjectREFR* ref, RE::BSFixedString* sound)
	{
		if (ref && *reinterpret_cast<const std::uint8_t*>(reinterpret_cast<const char*>(ref) + 0x1A) == 0x41 &&   // an actor
			AimSeesScene(ref->formID)) {                                                   // (the game's own test), in a scene
			if (overrideOn.load(std::memory_order_relaxed)) {
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
		bool reaches = true;
		for (const auto site : sites)
			reaches = Hook::WriteCall(site, reinterpret_cast<uintptr_t>(&HookResolve)) == *target && reaches;
		origResolve = reinterpret_cast<ResolveFn>(*target);
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

// ---- the engine's own sounds (A-67 v1, the owner 2026-10-01: "we need make working pipeline", the sounds are
// tuned later). Her side drives them: the depth Aim measured for her this frame (a shaft past her vagina's or anus's
// entrance); his depth mirrors hers and would double every sound. Played by EDID (Anatomy.esp's SNDRs) at her pelvis,
// following it.
namespace
{
	constexpr float kEnter = 0.3f;          // depth past the entrance that counts as inside
	constexpr float kTurn = 0.2f;           // a fall this far below the stroke's deepest point ends the stroke
	constexpr float kMinStroke = 1.0f;      // a stroke shallower than this makes no sound (a jiggle, not a thrust)
	constexpr float kHardSpeed = 60.0f;     // units per second: above this a stroke is a hard impact (RFAE 3)
	constexpr ULONGLONG kMinGapMs = 120;    // two stroke sounds never closer than this
	constexpr ULONGLONG kThrustEventMs = 250;   // RFAE 2 at most this often per actor (Rapport's ask)
	constexpr ULONGLONG kOutMs = 400;       // empty this long = penetration ended
	constexpr ULONGLONG kMaxStrokeMs = 3000;   // a longer gap is a pause, not a stroke: its period reads 0
	const char* kSlap = "AnatomySoundSlap";
	const char* kSquelch = "AnatomySoundSquelch";
	const char* kThrust = "AnatomySoundThrust";
	const char* kSlurp = "AnatomySoundSlurp";   // a shaft enters her mouth
	const char* kSuck = "AnatomySoundSuck";     // each oral stroke: the closed mouth moving
	const char* kSiphon = "AnatomySoundSiphon"; // now and then on top: air slipping between lips and skin (the owner 10-01:
	                                            // most of the sound is the closed mouth, the slurp is the accident)

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
	};
	std::unordered_map<std::uint64_t, Track> tracks;   // (formID, channel): the scan thread's own
	std::uint32_t rng = 0x9E3779B9u;

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
	};
	std::mutex voiceLock;
	std::vector<VoiceRequest> voiceQueue;
	std::unordered_map<UInt32, std::uint32_t> lastVoice;   // actor -> its last RFAP handle id (the scan thread's)
	std::atomic<std::uint32_t> voiced{ 0 };

	void PlayVoices()
	{
		std::vector<VoiceRequest> pending;
		{
			std::lock_guard<std::mutex> l(voiceLock);
			pending.swap(voiceQueue);
		}
		for (const VoiceRequest& r : pending) {
			Actor* a = nullptr;
			for (auto& e : actorEntries)
				if (e.actor && e.actor->formID == r.formID) {
					a = e.actor;
					break;
				}
			char key[64];
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
		UInt32 flags;     // RFAE's: oral
		bool sounds;      // this channel plays the body sounds
		bool mouth;       // at the head (slurp, suck) rather than the pelvis (squelch, slap, thrust)
	};

	// one channel's strokes: a stroke rises from its shallowest point and ends when the depth falls kTurn below its
	// deepest; the deepest point is the thrust
	void Step(Track& t, const Channel& c, Actor* a, ULONGLONG now, float deep)
	{
		const float d = c.depth;
		if (!t.inside) {
			if (d > kEnter) {   // penetration began
				t.inside = true;
				t.rising = true;
				t.trough = d;
				t.troughAt = now;
				t.emptySince = 0;
				if (c.sounds)
					c.mouth ? PlayByName(kSlurp, HeadOf(a), 1.0f, Jitter()) : PlayByName(kSquelch, PelvisOf(a), 1.0f, Jitter());
				SendEvent(c.formID, c.partner, 1, d, 0.0f, 0, c.flags);
			}
			t.depth = d;
			return;
		}
		if (d <= 0.05f) {       // empty: ended once it stays so
			if (!t.emptySince)
				t.emptySince = now;
			else if (now - t.emptySince > kOutMs) {
				t.inside = false;
				SendEvent(c.formID, c.partner, 4, 0.0f, 0.0f, 0, c.flags);
			}
			t.depth = d;
			return;
		}
		t.emptySince = 0;
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
			if (stroke >= kMinStroke && now - t.lastStroke >= kMinGapMs) {
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
						PlayByName(kSlap, pelvis, volume, Jitter());
						PlayByName(kThrust, pelvis, volume * 0.9f, Jitter());
					}
				}
				const UInt32 flags = c.flags | (t.peak >= deep ? FaceAuthority::kSoundEventDeep : 0u);
				if (now - t.lastThrustEvent >= kThrustEventMs) {
					t.lastThrustEvent = now;
					SendEvent(c.formID, c.partner, 2, t.peak, speed, strokeMs, flags);
				}
				if (speed > kHardSpeed)
					SendEvent(c.formID, c.partner, 3, t.peak, speed, strokeMs, flags);
			}
		}
		t.depth = d;
	}
}

namespace Sound
{
	std::uint32_t PlayedCount() { return played.load(); }
	std::uint32_t VoicedCount() { return voiced.load(); }

	void QueueVoice(UInt32 formID, UInt32 soundFormID, float volume, UInt32 flags)
	{
		std::lock_guard<std::mutex> l(voiceLock);
		if (voiceQueue.size() < 64)   // a runaway sender cannot grow it without bound
			voiceQueue.push_back({ formID, soundFormID, volume, flags });
	}

	void Update()
	{
		if (canPlay)
			PlayVoices();   // override on or off: Rapport voices its actors either way
		if (!canPlay || !overrideOn.load(std::memory_order_relaxed)) {
			tracks.clear();
			return;
		}
		const ULONGLONG now = GetTickCount64();
		const float deep = MouthDeepDepth();
		for (auto& e : actorEntries) {
			Actor* a = e.actor;
			if (!a || !AimSeesScene(a->formID))
				continue;
			const UInt32 id = a->formID;
			const bool entered = AimReceived(id);
			// channel 0, the genitals: the one entered (a shaft in the vagina or anus, any sex) plays the body sounds at
			// the pelvis; the shaft's owner's depth (vagina, anus or a mouth) only tells Rapport - it mirrors the
			// receiver's, whose channel plays the sound, so none doubles
			Channel genital{ id, AimPartner(id), AimDepth(id),
				entered ? FaceAuthority::kSoundEventReceiver : (AimInMouth(id) ? FaceAuthority::kSoundEventOral : 0u),
				entered, false };
			Step(tracks[(std::uint64_t)id << 1], genital, a, now, deep);
			// channel 1, the mouth: a shaft in this actor's mouth, sucking sounds at the head and oral events for
			// the mouth's owner (Rapport's gags)
			Channel mouth{ id, AimOralPartner(id), AimOralDepth(id),
				FaceAuthority::kSoundEventOral | FaceAuthority::kSoundEventReceiver, true, true };
			Step(tracks[((std::uint64_t)id << 1) | 1], mouth, a, now, deep);
		}
	}
}
