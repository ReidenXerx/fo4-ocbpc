// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-24: face authority, Rapport's faces applied after the merge.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The owner (2026-09-24): Rapport is the source of truth for the face of every actor it holds. The
// engine merges a face as final = max(override, animation) at +0x6689D0, so an override can open a
// feature the animation has closed but never close one the animation opens, and AAF's lock does not
// reach it. This plugin owns the one hook after that merge (the mouth's), so Rapport sends its faces
// here and they are written over the merged weights.
//
// What the merge does every frame the game runs (read out of 1.10.163, 2026-09-24): it recomputes the
// final weights from scratch (max of the two layers, or a copy of the animation's), and then sets the
// upper eyelids (18/41) to min(1, blink + animation), which is why an MFG override never reaches the
// eyelids. So a held face lasts exactly as long as it is written: the frame after a Clear the engine's
// own face is back. While the game is paused the merge computes nothing and the last face stays.
//
// Messages, F4SE messaging, sender "Rapport" -> receiver "OCBPC plugin" (agreed with the Rapport session):
//   'RFAS' Set   { u32 version = 1; u32 formID; u64 owned; float value[54]; }   232 bytes, values 0..1
//   'RFAC' Clear { u32 version = 1; u32 formID; }                                 formID 0 = everyone
// and back, this plugin -> "Rapport" at PostPostLoad:
//   'RFAH' Hello { u32 version = 1; u32 features; }                               no hello, no authority
// Bit i of owned: morph i is Rapport's; a morph it does not own keeps the engine's value. Bit 63: the
// actor is speaking, so the mouth morphs go back to the engine (lip sync) while the rest stays held.
// Rapport (2026-09-24) hands the mouth back the other way, by re-sending the face with its mouth bits
// cleared for the length of a line; both work, and the bit stays for whoever wants it.
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
	// keeps the larger of the two, so the eyes still close over a held look; while speaking, the mouth
	// morphs keep the engine's value.
	void Compose(float* weights, const Face& face);

	bool IsMouth(int id);

	// The self-test's face ([Mouth] authorityTest, our own messages standing in for Rapport's): every
	// expression morph held, the brows up, a smile, the eyelids a little down (a blink must still close
	// them) and the jaw at 0, so it stays shut even over an animation or lip sync that opens it.
	SetMessage TestFace(std::uint32_t formID);

	// The held faces (thread-safe: Rapport dispatches on its thread, the merge runs on another)
	void Set(std::uint32_t formID, const Face& face);
	void Clear(std::uint32_t formID);              // 0 = everyone
	std::vector<std::pair<std::uint32_t, Face>> Snapshot();
	std::size_t Count();
}
