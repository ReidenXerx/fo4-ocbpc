// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-30: engine-driven sex sounds (fo4-anatomy A-67).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// The owner's roadmap item 3: the body sounds of a scene come from what this engine measures, not from the packs'
// hand-placed ones, and the packs' own sounds (bodies AND voices, all "SoundPlay.<SNDR>" annotations in their .hkx)
// are muted for actors in a scene while the override is on. Rapport's MCM switches the override (RFAU); with no
// word from Rapport it stays OFF, so an old Rapport, or none, never silences anything.
//
// CommonLibF4RD has no audio API: the handle calls below were found 2026-09-30 from the Papyrus Sound natives and
// matched on AE (their ids and how, fo4-anatomy docs/decisions.md A-67). The mute is a call-site hook in the
// animation sound-event handler: its SoundPlay branch asks OG 0x16D490 / AE 0x324AD0 for the sound's descriptor,
// and a null descriptor plays nothing.
#include "INIReader.h"

#include "Game.h"

#include <string>

namespace Sound
{
	void LoadConfig(INIReader& reader);   // ocbp.ini [Sound]
	void Install();                       // the mute hook and the audio calls; says in cbp.log what it did
	void SetOverride(bool on, const char* who);   // RFAU (Rapport's MCM), or [Sound] force for a test
	bool Override();
	bool CanPlay();                       // the audio calls resolved for this build
	bool Hooked();                        // the SoundPlay mute is in place (the hello's bit 10)
	std::string OverrideSource();         // who set the override last (Rapport, [Sound] force), for the health check

	// A sound descriptor (SNDR) played at a node that it then follows; volume and frequency 1 = as authored.
	// Returns the handle's id (for Stop), or 0xFFFFFFFF when nothing plays.
	std::uint32_t PlayAt(RE::TESForm* sndr, RE::NiAVObject* follow, float volume = 1.0f, float frequency = 1.0f);
	void Stop(std::uint32_t id);

	std::uint32_t MutedCount();           // SoundPlay events dropped since the game started (the health check)
}
