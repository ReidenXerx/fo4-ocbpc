// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: face authority, Rapport's faces applied after the merge.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The owner (2026-09-24): Rapport is the source of truth for the face of every actor it holds. The
// engine merges a face as final = max(+0xF0, +0x1C8) at +0x6689D0, so a value in either layer can open
// a feature but never close one the other opens, and AAF's lock does not reach the merge. This plugin
// owns the one hook after that merge (the mouth's), so Rapport sends its faces here and they are
// written over the merged weights (FaceCompose.h has the order).
//
// The two layers, read out of 1.10.163 (2026-09-24):
//   +0xF0  MFG (the console, AAF's mfgSets, SAM) AND the lip sync of a line being spoken; when the
//          line ends the whole layer fades to 0, which is why an MFG expression is lost after a line
//   +0x1C8 the expression keyframes (idle faces, a line's own emotion)
// A line is playing while the lip-sync object at +0x2C0 is in state 3 or 4. The upper eyelids
// (18/41) are set after the merge to min(1, blink + animation), so no MFG value ever reaches them.
//
// Messages, F4SE messaging, sender "Rapport" -> receiver "OCBPC plugin" (agreed with the Rapport session):
//   'RFAS' Set   { u32 version; u32 formID; u64 owned; float value[54]; }   232 bytes, values 0..1
//   'RFAC' Clear { u32 version; u32 formID; }                               formID 0 = everyone
// and back, this plugin -> "Rapport" at PostPostLoad:
//   'RFAH' Hello { u32 version; u32 features; }                             no hello, no authority
// Versions: a reader takes any version from 1 up and reads the fields it knows; a later version only
// ever appends fields, and a new meaning for an old field goes behind a feature bit.
// Bit i of owned: morph i is Rapport's; a morph it does not own keeps the engine's value. A Set that
// owns no morph is a Clear. While the actor speaks (the engine's own lip state, or bit 63 set by the
// sender) the mouth morphs keep the engine's value, which is the line's lip sync.
namespace FaceAuthority
{
	constexpr std::uint32_t kSet = 0x52464153;      // 'RFAS'
	constexpr std::uint32_t kClear = 0x52464143;    // 'RFAC'
	constexpr std::uint32_t kHello = 0x52464148;    // 'RFAH'
	constexpr std::uint32_t kVersion = 1;
	constexpr std::uint32_t kFeatures = 1;          // bit 0: set/clear with the speaking bit
	constexpr int kMorphs = 54;
	constexpr int kSpeakingBit = 63;

	struct SetMessage
	{
		std::uint32_t version;
		std::uint32_t formID;
		std::uint64_t owned;
		float value[kMorphs];
	};
	struct ClearMessage
	{
		std::uint32_t version;
		std::uint32_t formID;
	};
	struct HelloMessage
	{
		std::uint32_t version;
		std::uint32_t features;
	};
	static_assert(sizeof(SetMessage) == 232, "the set message is 232 bytes");
	static_assert(offsetof(SetMessage, owned) == 8, "owned sits at offset 8");
	static_assert(offsetof(SetMessage, value) == 16, "the values start at offset 16");

	struct Face
	{
		std::uint64_t owned = 0;
		float value[kMorphs] = {};
	};

	enum class Command { None, Set, Clear };
	struct Decoded
	{
		Command command = Command::None;
		std::uint32_t formID = 0;
		Face face;
		const char* refused = nullptr;              // why a message was not taken, for the log
	};

	// Reads one message. Free of F4SE, so it runs in a test outside the game.
	Decoded Decode(std::uint32_t type, const void* data, std::uint32_t length);

	// Writes a held face over the merged weights: owned morphs replace the merge; the blink (18/41)
	// keeps the larger of the two, so the eyes still close over a held look; while the actor speaks
	// (`speaking`, the engine's lip state, or the face's bit 63) the mouth morphs keep the engine's value.
	void Compose(float* weights, const Face& face, bool speaking);

	// Rapport's MOUTH set (fo4-rapport tools/make_mfg.py): what a line's lip sync may need
	bool IsMouth(int id);

	// The self-test's face ([Face] test, our own messages standing in for Rapport's): every expression
	// morph held, the brows up, a smile, the eyelids a little down (a blink must still close them) and
	// the jaw at 0, so it stays shut even over an animation that opens it.
	SetMessage TestFace(std::uint32_t formID);

	// The held faces (thread-safe: Rapport dispatches on its thread, the merge runs on another)
	void Set(std::uint32_t formID, const Face& face);
	void Clear(std::uint32_t formID);              // 0 = everyone
	std::vector<std::pair<std::uint32_t, Face>> Snapshot();
}
