// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-30: engine-driven sex sounds (fo4-anatomy A-67).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Sound.h"

#include "Aim.h"
#include "CollisionHub.h"
#include "Hook.h"

#include <windows.h>
#include <atomic>
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
			rFreq = Hook::Resolve(kSetFrequency), rFollow = Hook::Resolve(kFollow);
		if (rMgr && rBuild && rPlay && rStop && rVol && rFreq && rFollow) {
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
		const auto site = Hook::CallSite(kEventHandler, kResolveSoundPlay, "[sound] the SoundPlay descriptor's call");
		const auto target = Hook::Resolve(kResolveSoundPlay);
		if (!site || !target) {
			Note("sound|build", "[sound] this game build is not proven for the SoundPlay mute (call %d, lookup %d): the "
				"packs' sounds play as they are\n", (int)site.has_value(), (int)target.has_value());
			return;
		}
		origResolve = reinterpret_cast<ResolveFn>(Hook::WriteCall(*site, reinterpret_cast<uintptr_t>(&HookResolve)));
		hooked = reinterpret_cast<uintptr_t>(origResolve) == *target;
		Note("sound|on", hooked ? "[sound] the SoundPlay descriptor's call hooked (its code untouched): the packs' sounds "
			"are muted in scenes while the override is on (Rapport's MCM; off until Rapport says so)\n"
			: "[sound] the hooked call did not return the descriptor lookup: nothing is muted\n");
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
				on && !hooked ? ", but the mute is not hooked: the packs' sounds still play" : "");
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
