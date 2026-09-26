// fo4-anatomy: the fail-closed layout guard of the Runtime Database build (Hook.h). Runtime Database finds functions on
// every runtime; it does not make a class's layout the same (its docs/FEATURES.md), and it defines no BSGeometry or
// BSSkin::Instance at all, so the offsets in G::Measured are 1.10.163's. They are proven here, on the running game,
// by a test that cannot pass by accident: every transform slot of a skin must point at the world transform of the
// node in the matching bone slot.
#include "PCH.h"
#include "Hook.h"

namespace
{
	Layout::State skin = Layout::State::kUnchecked;

	struct Probe
	{
		RE::BSGeometry* geometry;
		int checked;
		const char* problem;
	};

	void Read(void* a_context)
	{
		auto& p = *static_cast<Probe*>(a_context);
		G::SkinInstance* inst = G::Skin(p.geometry);
		if (!inst) {
			return;   // not skinned: nothing to learn from this one
		}
		auto& bones = G::SkinBones(inst);
		auto& transforms = G::SkinWorldTransforms(inst);
		if (!bones.entries || !transforms.entries || bones.count == 0 || bones.count > 1024 || transforms.count != bones.count) {
			p.problem = "bone and transform arrays do not match";
			return;
		}
		for (std::uint32_t i = 0; i < bones.count; i++) {
			NiNode* bone = bones.entries[i];
			if (!bone) {
				continue;   // an empty slot is legal (a bone this skeleton lacks)
			}
			if (bone->IsNode() != bone) {
				p.problem = "a bone slot does not hold a node";
				return;
			}
			if (transforms.entries[i] != &G::World(bone)) {
				p.problem = "a transform slot does not point at its bone's world transform";
				return;
			}
			p.checked++;
		}
	}

	bool Guarded(void (*a_fn)(void*), void* a_context)
	{
		__try {
			a_fn(a_context);
			return true;
		} __except (1) {
			return false;
		}
	}
}

namespace Layout
{
	State Skin() { return skin; }

	void CheckSkin(RE::BSGeometry* a_geometry)
	{
		if (skin != State::kUnchecked || !a_geometry) {
			return;
		}
		Probe probe{ a_geometry, 0, nullptr };
		if (!Guarded(&Read, &probe)) {
			probe.problem = "reading the skin faulted";
		}
		if (probe.problem) {
			skin = State::kBad;
			rdlog::error("layout: BSGeometry/BSSkin::Instance differ on this runtime ({}): the genital bones and their "
			             "collisions are OFF for this session",
				probe.problem);
		} else if (probe.checked >= 8) {
			skin = State::kGood;
			rdlog::info("layout: a skin's {} bone slots and transform slots check out (BSGeometry +0x{:X}, bones +0x{:X}, "
			            "transforms +0x{:X})",
				probe.checked, G::Measured::kGeometrySkinInstance, G::Measured::kSkinBones, G::Measured::kSkinWorldTransforms);
		}
	}
}
