// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16 (see Strands.h; the design is OStrands').
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Strands.h"

#include <array>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "ActorEntry.h"
#include "Aim.h"
#include "Hook.h"
#include "NiMath.h"

#include <windows.h>

namespace Strands
{
	namespace
	{
		constexpr int kBones = 10;
		constexpr const char* kMesh = "Anatomy\\Strand\\Strand.nif";   // BSModelDB puts "meshes\" in front

		// BSModelDB::DBTraits::ArgsType (CommonLibF4): lod fade mult, load level, then the flag bits. The AE callers
		// set prepareAfterLoad, useErrorMarker, performProcess and loadTextures (byte 0x2D) and clear the rest.
		struct ModelArgs
		{
			std::int32_t lodFadeMult{ 0 };
			std::uint32_t loadLevel{ 0 };
			std::uint8_t flags{ 0x01 | 0x08 | 0x20 };   // prepareAfterLoad, performProcess, loadTextures
			std::uint8_t pad[3]{};
		};
		static_assert(sizeof(ModelArgs) == 0xC);
		// BSModelDB::Demand(name, NiPointer<NiNode>*, args): OG 1225688 (CommonLibF4); AE 2275154, found 2026-10-16
		// by the callers that load the same literal paths (Note01.nif, WorkshopMenuGlass.nif) and use its result as a
		// NiPointer with the same flag bits (fo4-anatomy scratch find_demand_ae3.py / ae_demand_callers.py)
		using Demand_t = std::uint32_t (*)(const char*, RE::NiPointer<NiNode>*, const ModelArgs&);
		constexpr Hook::IdPair kDemand{ 1225688, 2275154 };

		struct Params
		{
			bool enabled = true;
			// OStrands' [Shape] and [Physics] defaults where they apply (its OStrands.ini)
			float chance = 0.6f;        // a pull-out leaves a strand this often
			float maxStart = 20.0f;     // units from her entrance to his tip when Aim lets go: farther, no strand (the
			                            // lock lets go with the tip ~11 out, 10-16)
			float maxLength = 14.0f;    // stretched this far past its first length, it breaks (OStrands' rule)
			float radius = 0.12f;       // at its first length; its volume is kept, so it thins as it stretches
			float minRadius = 0.035f;   // thinner than this, it breaks
			float thinning = 0.35f;     // the middle is (1 - this) of the ends
			float slack = 1.15f;        // rest length over the gap it formed at: it sags
			float gravity = 350.0f;     // units per second squared (a viscous strand falls slower than a stone)
			float damping = 0.9f;       // per frame, of the verlet velocity
			float life = 4.0f;          // seconds: a strand still whole by then breaks
			float fade = 0.4f;          // seconds to thin to nothing once broken
		} P;

		// the one strand (one Demand prototype: a second at once needs a clone, OStrands' Clone())
		struct Strand
		{
			bool active = false;
			bool ending = false;
			UInt32 owner = 0;           // her: the opening's owner, whose 3D carries the strand
			UInt32 partner = 0;         // the shaft's owner: the far end is his tip
			int kind = 0;               // the opening: 0 vagina, 1 anus, 2 mouth
			float rest = 1.0f;          // per segment
			float startLength = 1.0f;   // the gap it formed at
			float volume = 0.0f;        // kept: radius = sqrt(volume / (pi * length))
			float age = 0.0f;
			float fadeLeft = 0.0f;
			std::array<NiPoint3, kBones> p{}, prev{};
		} S;

		RE::NiPointer<NiNode> root;
		std::array<NiAVObject*, kBones> bones{};
		NiNode* attachedTo = nullptr;
		bool failed = false;
		std::uint32_t rng = 0x2545F491u;
		LARGE_INTEGER lastTick{};
		int logged = 0;   // the first starts and ends of a session, in the log
		std::mutex lock;  // the scan thread's Tick against a load's Reset (Canal.cpp, Skirt.cpp: the same)

		// a pull-out: an opening that held a shaft last frame and holds none now (key: owner << 2 | kind)
		struct Hold
		{
			UInt32 partner = 0;
			bool held = false;
		};
		std::unordered_map<std::uint64_t, Hold> holds;

		float Random01()
		{
			rng = rng * 1664525u + 1013904223u;
			return (float)((rng >> 8) & 0xFFFF) / 65535.0f;
		}

		NiPoint3 Cross(const NiPoint3& a, const NiPoint3& b)
		{
			return NiPoint3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
		}

		float Length(const NiPoint3& v)
		{
			return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		}

		NiPoint3 Unit(const NiPoint3& v)
		{
			const float l = Length(v);
			return l > 1e-6f ? v * (1.0f / l) : NiPoint3(1, 0, 0);
		}

		// a world transform whose local X runs along `dir` (the engine keeps an object's axes in the ROWS: Aim.cpp)
		NiTransform Frame(const NiPoint3& at, const NiPoint3& dir, float scale)
		{
			const NiPoint3 x = Unit(dir);
			const NiPoint3 up = std::fabs(x.z) < 0.9f ? NiPoint3(0, 0, 1) : NiPoint3(0, 1, 0);
			const NiPoint3 y = Unit(Cross(up, x));
			const NiPoint3 z = Cross(x, y);
			NiTransform t;
			const NiPoint3 rows[3] = { x, y, z };
			for (int r = 0; r < 3; r++) {
				t.rot.data[r][0] = rows[r].x;
				t.rot.data[r][1] = rows[r].y;
				t.rot.data[r][2] = rows[r].z;
			}
			t.pos = at;
			t.scale = scale;
			return t;
		}

		// local and world at once (Canal.cpp style), under a parent whose world is pw
		void Place(NiAVObject* a_obj, const NiTransform& w, const NiTransform& pw)
		{
			NiTransform& lt = G::Local(a_obj);
			lt.rot = w.rot * pw.rot.Transpose();
			lt.pos = (pw.rot * (w.pos - pw.pos)) * (1.0f / pw.scale);
			lt.scale = w.scale / pw.scale;
			G::World(a_obj) = w;
		}

		Actor* ActorOf(UInt32 formID)
		{
			for (auto& e : actorEntries)
				if (e.actor && e.actor->formID == formID)
					return e.actor;
			return nullptr;
		}

		bool Load()
		{
			const auto fn = Hook::Resolve(kDemand);
			if (!fn) {
				spdlog::info("[strands] BSModelDB::Demand not resolved on this executable; strands off");
				return false;
			}
			RE::NiPointer<NiNode> model;
			const ModelArgs args{};
			const auto error = reinterpret_cast<Demand_t>(*fn)(kMesh, &model, args);
			if (error != 0 || !model) {
				spdlog::info("[strands] could not load meshes\\{} (error {}); strands off", kMesh, error);
				return false;
			}
			for (int i = 0; i < kBones; i++) {
				char name[16];
				std::snprintf(name, sizeof(name), "Strand%02d", i);
				bones[i] = G::Find(model.get(), name);
				if (!bones[i]) {
					spdlog::info("[strands] meshes\\{} has no {}; strands off", kMesh, name);
					return false;
				}
			}
			// the identity, spelt out: NiTransform() zero-fills its matrix (NiMatrix43() : arr{}), and a zero rotation
			// let the game's own update collapse every child onto the root each frame (2026-10-16, runs 1-6)
			NiTransform identity;
			identity.rot.data[0][0] = identity.rot.data[1][1] = identity.rot.data[2][2] = 1.0f;
			G::Local(model.get()) = identity;
			root = model;
			spdlog::info("[strands] loaded meshes\\{}", kMesh);
			return true;
		}

		void Detach()
		{
			if (root && root->parent)
				root->parent->DetachChild(root.get());
			attachedTo = nullptr;
		}

		// under her 3D. A shape draws only when AttachChild itself puts it into the live tree under a root the game
		// processed (an actor's 3D): one that arrives inside our root, attached later, never draws, nor does any under
		// the cell's node or a fade node of ours (2026-10-16, tests 13-20). So every shape is detached and attached again
		bool Attach(NiNode* a_parent)
		{
			if (attachedTo == a_parent && root->parent == a_parent)
				return true;
			Detach();
			a_parent->AttachChild(root.get(), true);
			for (UInt16 i = 0; i < G::ChildCount(root.get()); i++) {
				NiAVObject* c = G::Child(root.get(), i);
				if (c && !G::AsNode(c)) {
					RE::NiPointer<NiAVObject> keep(c);   // Detach releases a reference
					root->DetachChild(c);
					root->AttachChild(c, true);         // the hole Detach left: the same slot
				}
			}
			attachedTo = a_parent;
			return true;
		}

		void End()
		{
			if (S.active && logged < 40) {
				logged++;
				spdlog::info("[strands] ended: {:08X} (opening {}) to {:08X} after {:.1f} s", S.owner, S.kind, S.partner, S.age);
			}
			S.active = false;
			Detach();
		}

		void Start(UInt32 a_owner, int a_kind, UInt32 a_partner, const NiPoint3& a, const NiPoint3& b)
		{
			S = Strand{};
			S.active = true;
			S.owner = a_owner;
			S.kind = a_kind;
			S.partner = a_partner;
			S.startLength = (std::max)(Length(b - a), 0.5f);
			S.rest = S.startLength * P.slack / (kBones - 1);
			S.volume = 3.14159265f * P.radius * P.radius * S.startLength;
			for (int i = 0; i < kBones; i++) {
				const float t = (float)i / (kBones - 1);
				S.p[i] = S.prev[i] = a + (b - a) * t;
			}
			if (logged < 40) {
				logged++;
				spdlog::info("[strands] a strand: {:08X} (opening {}) to {:08X}, {:.1f} apart", a_owner, a_kind, a_partner,
					Length(b - a));
			}
		}

		// the ends this frame: her opening's entrance, his tip; false when either is gone
		bool Ends(NiPoint3& a, NiPoint3& b)
		{
			bool found = false;
			for (const AimOpening& o : AimOpenings())
				if (o.owner == S.owner && o.kind == S.kind) {
					a = o.point;
					found = true;
				}
			Actor* him = ActorOf(S.partner);
			return found && him && AimTip(him, b);
		}

		void Simulate(float dt, const NiPoint3& a, const NiPoint3& b)
		{
			S.p[0] = a;
			S.p[kBones - 1] = b;
			const NiPoint3 g(0, 0, -P.gravity * dt * dt);
			for (int i = 1; i < kBones - 1; i++) {
				const NiPoint3 v = (S.p[i] - S.prev[i]) * P.damping;
				S.prev[i] = S.p[i];
				S.p[i] = S.p[i] + v + g;
			}
			// a rope, not a rod: a segment pulls when longer than its rest, never pushes
			for (int it = 0; it < 12; it++) {
				for (int i = 0; i < kBones - 1; i++) {
					const NiPoint3 d = S.p[i + 1] - S.p[i];
					const float l = Length(d);
					if (l <= S.rest || l < 1e-6f)
						continue;
					const NiPoint3 fix = d * ((l - S.rest) / l);
					if (i == 0)
						S.p[i + 1] = S.p[i + 1] - fix;
					else if (i + 1 == kBones - 1)
						S.p[i] = S.p[i] + fix;
					else {
						S.p[i] = S.p[i] + fix * 0.5f;
						S.p[i + 1] = S.p[i + 1] - fix * 0.5f;
					}
				}
			}
		}

		void Pose(const NiTransform& pw, float a_radius)
		{
			NiPoint3 lo = S.p[0], hi = S.p[0];
			for (int i = 0; i < kBones; i++) {
				const NiPoint3 dir = i + 1 < kBones ? S.p[i + 1] - S.p[i] : S.p[i] - S.p[i - 1];
				const float t = (float)i / (kBones - 1);
				const float r = a_radius * (1.0f - P.thinning * std::sin(3.14159265f * t));
				Place(bones[i], Frame(S.p[i], dir, (std::max)(r, 0.001f)), pw);
				lo = NiPoint3((std::min)(lo.x, S.p[i].x), (std::min)(lo.y, S.p[i].y), (std::min)(lo.z, S.p[i].z));
				hi = NiPoint3((std::max)(hi.x, S.p[i].x), (std::max)(hi.y, S.p[i].y), (std::max)(hi.z, S.p[i].z));
			}
			G::World(root.get()) = pw;
			// the engine's own update down our tree (OStrands' root->Update), then the bounds by hand: the shape's model
			// bound is around the rest pose, and the drawn tests set them so
			RE::NiUpdateData update{};
			root->UpdateDownwardPass(update, 0);
			const NiPoint3 c = (lo + hi) * 0.5f;
			const float radius = Length(hi - lo) * 0.5f + P.radius * 2.0f;
			for (UInt16 i = 0; i < G::ChildCount(root.get()); i++)
				if (NiAVObject* s = G::Child(root.get(), i))
					if (!G::AsNode(s)) {
						s->worldBound.center.x = c.x, s->worldBound.center.y = c.y, s->worldBound.center.z = c.z;
						s->worldBound.fRadius = radius;
					}
			root->worldBound.center.x = c.x, root->worldBound.center.y = c.y, root->worldBound.center.z = c.z;
			root->worldBound.fRadius = radius;
		}

		void TickImpl()
		{
			std::lock_guard<std::mutex> guard(lock);
			if (!P.enabled) {   // switched off (or an exception) with a strand up: it goes
				End();
				return;
			}
			LARGE_INTEGER now, freq;
			QueryPerformanceCounter(&now);
			QueryPerformanceFrequency(&freq);
			float dt = lastTick.QuadPart ? (float)(now.QuadPart - lastTick.QuadPart) / (float)freq.QuadPart : 0.0f;
			lastTick = now;
			dt = (std::min)((std::max)(dt, 0.0f), 1.0f / 30.0f);

			// pull-outs: every opening, what holds it now against last frame. Not only those in a scene: Aim locks a
			// shaft only in one, and the last pull-out is as the scene ends, when AAF's busy flag may already be gone
			// (10-16: a 40 s Locker Sex scene ended with no strand)
			for (const AimOpening& o : AimOpenings()) {
				if (o.kind < 0 || o.kind > 2)
					continue;
				const std::uint64_t key = ((std::uint64_t)o.owner << 2) | (unsigned)o.kind;
				const UInt32 partner = o.kind == 2 ? AimOralPartner(o.owner)
				                                   : ((AimReceivedKinds(o.owner) >> o.kind) & 1u) ? AimPartner(o.owner) : 0u;
				Hold& h = holds[key];
				if (partner) {
					h.held = true;
					h.partner = partner;
					if (S.active && !S.ending && S.owner == o.owner && S.kind == o.kind)
						S.ending = true, S.fadeLeft = 0.0f;   // back in: the strand is swallowed at once
					continue;
				}
				if (h.held && h.partner != o.owner) {
					NiPoint3 tip;
					Actor* him = ActorOf(h.partner);
					const bool hasTip = him && AimTip(him, tip);
					const float gap = hasTip ? Length(tip - o.point) : -1.0f;
					const bool roll = !S.active && Random01() < P.chance;
					if (logged < 40) {
						logged++;
						spdlog::info("[strands] pull-out: {:08X} (opening {}, in scene {}) from {:08X}, tip {:.1f} away; {}",
							o.owner, o.kind, o.inScene, h.partner, gap,
							S.active ? "a strand is already up" : !roll ? "no strand this time (chance)" :
							!hasTip ? "his tip is not found" : gap >= P.maxStart ? "too far already" : "a strand");
					}
					if (roll && hasTip && gap < P.maxStart)
						Start(o.owner, o.kind, h.partner, o.point, tip);
				}
				h.held = false;
			}
			if (holds.size() > 256)
				holds.clear();

			if (!S.active)
				return;
			NiPoint3 a, b;
			Actor* her = ActorOf(S.owner);
			NiNode* parent = her ? G::AsNode(G::Root(her)) : nullptr;
			if (!parent || !Ends(a, b)) {
				End();
				return;
			}
			S.age += dt;
			if (!root && (failed || !Load())) {   // the mesh is loaded at the first strand, once
				failed = true;
				S.active = false;
				return;
			}
			Attach(parent);
			Simulate(dt, a, b);
			// OStrands' rule: its volume kept over its length; it breaks stretched maxLength past its first length,
			// thinner than minRadius, or older than life
			float total = 0.0f;
			for (int i = 0; i + 1 < kBones; i++)
				total += Length(S.p[i + 1] - S.p[i]);
			const float radius = std::sqrt(S.volume / (3.14159265f * (std::max)(total, 0.01f)));
			if (!S.ending && (total - S.startLength > P.maxLength || radius < P.minRadius || S.age > P.life)) {
				S.ending = true;
				S.fadeLeft = P.fade;
			}
			float fadeScale = 1.0f;
			if (S.ending) {
				if (S.fadeLeft <= 0.0f) {
					End();
					return;
				}
				fadeScale = S.fadeLeft / P.fade;
				S.fadeLeft -= dt;
			}
			Pose(G::World(parent), radius * fadeScale);
		}
	}

	void LoadConfig(INIReader& reader)
	{
		P = Params();
		P.enabled = reader.GetBoolean("Strands", "enabled", true);
		auto f = [&](const char* k, float d) { return (float)reader.GetReal("Strands", k, d); };
		P.chance = (std::min)(1.0f, (std::max)(0.0f, f("chance", P.chance)));
		P.maxStart = (std::max)(1.0f, f("maxStart", P.maxStart));
		P.maxLength = (std::max)(1.0f, f("maxLength", P.maxLength));
		P.radius = (std::max)(0.01f, f("radius", P.radius));
		P.minRadius = (std::max)(0.001f, f("minRadius", P.minRadius));
		P.thinning = (std::min)(0.95f, (std::max)(0.0f, f("thinning", P.thinning)));
		P.slack = (std::max)(1.0f, f("slack", P.slack));
		P.gravity = f("gravity", P.gravity);
		P.damping = (std::min)(1.0f, (std::max)(0.0f, f("damping", P.damping)));
		P.life = (std::max)(0.5f, f("life", P.life));
		P.fade = (std::max)(0.01f, f("fade", P.fade));
	}

	void Tick()
	{
		if (!P.enabled && !S.active)
			return;
		__try {
			TickImpl();
		} __except (1) {
			static bool said = false;
			if (!said) {
				said = true;
				spdlog::info("[strands] an exception in the strand update: strands off for this session");
			}
			P.enabled = false;
		}
	}

	void Reset()
	{
		std::lock_guard<std::mutex> guard(lock);
		if (S.active || attachedTo)
			Detach();
		S = Strand{};
		holds.clear();
	}
}
