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
//   'RFAD' Deep  { u32 version; u32 formID; u64 mask; float value[54]; }    232 bytes, as a Set (A-29)
// A Deep is the face at FULL depth of oral contact for the ids in mask, for a face already held. It
// follows the Set it belongs to; a later Set for that actor drops it; a Clear clears both; a Deep that
// masks nothing drops it. Only when the hello has kFeatureDepthBlend.
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
	constexpr std::uint32_t kDeep = 0x52464144;     // 'RFAD'
	constexpr std::uint32_t kKnobs = 0x5246414B;    // 'RFAK': the fork's knobs from Rapport's MCM ("Bodies & faces")
	constexpr std::uint32_t kGlance = 0x52464147;   // 'RFAG': one actor looks into another's eyes for a while
	constexpr std::uint32_t kVersion = 1;
	// The hello's features: what this build does with a held face, so the sender can rely on it
	constexpr std::uint32_t kFeatureSetClear = 1u << 0;      // set/clear, and the speaking bit (63)
	constexpr std::uint32_t kFeatureEngineLines = 1u << 1;   // while the engine plays a line on a held face,
	                                                         // the MOUTH ids are its lip sync: no need to
	                                                         // clear them or set bit 63 for a line
	constexpr std::uint32_t kFeatureReaction = 1u << 2;      // during oral contact the reaction (A-26) may
	                                                         // RAISE brows, cheeks and nose above a held face
	constexpr std::uint32_t kFeatureDepthBlend = 1u << 3;    // a held face blends toward its Deep face by the
	                                                         // depth of oral contact (A-29)
	// bit 4 (16): glances (RFAG) turn the eyes. Set only once the owner has seen it work (Eyes.h, [Eyes] glances)
	constexpr std::uint32_t kFeatureGlances = 1u << 4;
	constexpr std::uint32_t kFeatureKnobs = 1u << 5;         // RFAK is applied (the MCM's page does something)
	constexpr std::uint32_t kFeatureGenitalDepth = 1u << 6;  // the Deep face also blends by the depth of a shaft
	                                                         // in her vagina or anus, and for him by his own
	                                                         // depth in any opening (needs [Aim] on)
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
	// RFAK (agreed with Rapport, 2026-09-25): sent after the hello and on every change of its MCM page. Fields
	// only ever append after version. Until one arrives the ini's values stand.
	struct KnobsMessage
	{
		std::uint32_t version;
		std::uint32_t enabled;       // bits: 0 aim, 1 shape, 2 lip fit, 3 face reaction (A-26), 4 deep blend (A-29)
		float lipClearance;          // the lips' clearance off what is inside
		float lipSpeed;              // x the lips' open and close rates
		float shaftScale;            // A-31: the shaft's scale
		float headMin, headMax;      // A-31: each man's head scale, in this range
		float reactScale;            // x the A-26 face terms
	};
	static_assert(sizeof(KnobsMessage) == 32, "the knobs message is 32 bytes");
	constexpr std::uint32_t kKnobAim = 1u << 0, kKnobShape = 1u << 1, kKnobLipFit = 1u << 2,
		kKnobReaction = 1u << 3, kKnobDeep = 1u << 4;
	struct Knobs
	{
		std::uint32_t enabled = 0x1F;
		float lipClearance = 0.05f, lipSpeed = 1.0f, shaftScale = 0.85f, headMin = 1.2f, headMax = 1.25f,
			reactScale = 1.0f;
	};
	// RFAG (agreed with Rapport, 2026-09-25): looker looks into target's eyes for durationMs, its upper lids
	// held at least lidsOpen open (1 wide open, 0 as they are). target 0: stop now. A new glance for the
	// same looker replaces the last. flags: none yet (sent 0).
	struct GlanceMessage
	{
		std::uint32_t version;
		std::uint32_t looker;
		std::uint32_t target;
		std::uint32_t durationMs;
		float lidsOpen;
		std::uint32_t flags;
	};
	static_assert(sizeof(GlanceMessage) == 24, "the glance message is 24 bytes");
	struct Glance
	{
		std::uint32_t target = 0;
		std::uint32_t durationMs = 0;       // clamped 100 .. 10000
		float lidsOpen = 0.0f;              // clamped 0 .. 1
	};
	static_assert(sizeof(SetMessage) == 232, "the set message is 232 bytes");
	static_assert(offsetof(SetMessage, owned) == 8, "owned sits at offset 8");
	static_assert(offsetof(SetMessage, value) == 16, "the values start at offset 16");

	struct Face
	{
		std::uint64_t owned = 0;
		float value[kMorphs] = {};
		std::uint64_t deepMask = 0;                 // A-29: the ids that blend toward deep[] with depth
		float deep[kMorphs] = {};
	};

	enum class Command { None, Set, Clear, Deep, Knobs, Glance };
	struct Decoded
	{
		Command command = Command::None;
		std::uint32_t formID = 0;                   // Command::Glance: the looker
		Face face;
		Knobs knobs;                                // Command::Knobs: clamped to sane ranges
		Glance glance;                              // Command::Glance: target 0 = stop
		const char* refused = nullptr;              // why a message was not taken, for the log
	};

	// Reads one message. Free of F4SE, so it runs in a test outside the game.
	Decoded Decode(std::uint32_t type, const void* data, std::uint32_t length);

	// Writes a held face over the merged weights: owned morphs replace the merge; the blink (18/41)
	// keeps the larger of the two, so the eyes still close over a held look; while the actor speaks
	// (`speaking`, the engine's lip state, or the face's bit 63) the mouth morphs keep the engine's value.
	void Compose(float* weights, const Face& face, bool speaking);

	// A-29, after Compose: each id in the face's deepMask goes from what the held face gave toward its
	// deep value by w (0 no contact .. 1 full depth). MOUTH ids are never blended (the contact mouth and a
	// line's lip sync own them); the blink keeps the larger of the engine's and the blend. `engine` is the
	// engine's own merged weights (FaceCompose keeps them before anything is written).
	void BlendDeep(float* weights, const float* engine, const Face& face, float w);

	// Rapport's MOUTH set (fo4-rapport tools/make_mfg.py): what a line's lip sync may need
	bool IsMouth(int id);

	// The self-test's face ([Face] test, our own messages standing in for Rapport's): every expression
	// morph held, the brows up, a smile, the eyelids a little down (a blink must still close them) and
	// the jaw at 0, so it stays shut even over an animation that opens it.
	SetMessage TestFace(std::uint32_t formID);

	// The held faces (thread-safe: Rapport dispatches on its thread, the merge runs on another)
	void Set(std::uint32_t formID, const Face& face);
	void Clear(std::uint32_t formID);              // 0 = everyone
	// A-29: the deep face of a face already held (false: none is held for that form, and nothing is kept)
	bool SetDeep(std::uint32_t formID, std::uint64_t mask, const float* values);
	std::vector<std::pair<std::uint32_t, Face>> Snapshot();
	// RFAK: the last knobs Rapport sent (thread-safe). False until one arrives, and then the ini's values
	// stand. A knob only ever switches OFF what the ini turned on: the ini's off is a config that failed.
	void SetKnobs(const Knobs& knobs);
	bool CurrentKnobs(Knobs& out);
	// RFAG (thread-safe): a glance starts at nowMs; target 0 stops the looker's. Glances(nowMs) is every
	// glance still running then (a finished one is forgotten); a load clears them with Clear(0).
	struct Running
	{
		std::uint32_t looker = 0;
		Glance glance;
		std::uint64_t startMs = 0;
	};
	void SetGlance(std::uint32_t looker, const Glance& glance, std::uint64_t nowMs);
	std::vector<Running> Glances(std::uint64_t nowMs);
}
