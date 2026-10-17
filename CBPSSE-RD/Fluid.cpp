// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16 (see Fluid.h; the design is OStrands' FluidManager,
// GPL-3, by LilRicefield: its emitters, reservoirs, domains and drawing are followed here, its constants kept).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Fluid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ActorEntry.h"
#include "ActorUtils.h"
#include "Aim.h"
#include "CollisionHub.h"
#include "FaceAuthority.h"
#include "FluidSim.h"
#include "Hook.h"
#include "NiMath.h"

#include <windows.h>

namespace Fluid
{
	namespace
	{
		using FluidSim::kNone;
		using FluidSim::SurfacePoint;
		using V = NiPoint3;

		constexpr float kPi = 3.14159265f;
		// OStrands' FluidManager constants. The particle world is in metres (FluidSim.h), the game's in units
		constexpr float kUnitsPerMeter = 70.0f;
		constexpr float kSpacing = 0.006f;
		constexpr float kVolume = kSpacing * kSpacing * kSpacing;
		constexpr float kKernel = 2.0f * kSpacing;
		constexpr float kThreshold = 1.75f;      // the field's value at a blob's skin
		constexpr float kBulkEnter = 3.0f, kBulkLeave = 2.5f;
		constexpr float kCovered = 0.3f;         // a lone particle inside a blob is not drawn again as a drop
		constexpr float kPieceRadius = 4.0f * kSpacing;
		constexpr float kSlotSpacing = 0.9f, kDribbleSpacing = 0.6f;
		constexpr std::size_t kBlobMinimum = 6;
		constexpr float kSmoothing = 0.05f;      // seconds: a blob's skin follows its particles this fast
		constexpr float kStep = 1.0f / 120.0f;
		constexpr int kMaxSteps = 6;
		constexpr std::size_t kMaxDomains = 4;
		constexpr float kDomainReach = 300.0f, kDomainStep = 80.0f;   // units
		constexpr float kStreak = 1.5f, kStreakStart = 0.3f, kMaxStretch = 3.0f, kSquash = 0.2f;
		constexpr float kGlobDisc = 0.8f;
		constexpr float kNozzleOffset = 0.4f;    // units past his tip
		constexpr float kMaxNozzleSpeed = 2.0f;  // m/s of his tip carried into the stream at most
		constexpr float kCarry = 0.6f, kCarrySmoothing = 0.08f;
		constexpr float kTailStart = 0.55f, kTailSlowdown = 0.85f;
		constexpr float kOozeSpeed = 0.15f;
		constexpr float kHoleRadius = 1.5f, kNozzleHole = 1.0f;   // units (an opening sits inside the lower belly capsule;
		                                                         // 3 let what came out at her entrance fall through her)
		// her canals (units): how deep (vagina, anus, mouth), how narrow when nothing is in it, how fast it closes after
		// he pulls out (the squeeze that gushes), and the room it leaves around his shaft while he is in (2.5 spacings)
		constexpr float kCanalDepth[3] = { 6.5f, 8.5f, 4.0f };
		constexpr float kCanalShut[3] = { 0.65f, 0.55f, 0.7f };
		constexpr float kCanalClose = 1.2f;
		constexpr float kCanalAround = 2.5f;
		constexpr float kExitDepth = 0.6f;       // units outside the entrance a reservoir comes out
		constexpr float kGushShare = 0.35f, kGushRate = 40.0f, kSeep = 0.03f, kLeak = 0.12f, kLeakSpeed = 0.1f;
		constexpr float kHangTime = 4.0f;
		constexpr int kGlobMin = 12, kGlobMax = 40;
		constexpr float kPlugLateral = 6.0f, kPlugDepth = 40.0f;  // units: his tip this near her axis, this deep, holds it in
		constexpr float kUnplugAfter = 0.6f;     // seconds out before it counts as a pull-out (Aim's lock flickers)
		constexpr float kDressedGrace = 2.5f;    // seconds dressed before her reservoir stops (OStrands)
		constexpr float kLostOpening = 2.0f;     // seconds an opening may be missing before its reservoir stops

		constexpr int kBlobPool = 48;   // meshes\Anatomy\Fluid\Blob00..47.nif: one Demand prototype each (Strands.cpp)
		constexpr int kRopePool = 32;   // meshes\Anatomy\Fluid\Rope00..31.nif
		constexpr int kMaxRopeBones = 32;

		struct Params
		{
			bool enabled = true;
			float amount = 1.5f;        // OStrands' semenAmount: particles per pulse x40 x this
			float strength = 1.0f;      // the pulses' speed
			int pulses = 5;
			float cohesion = 15.0f;
			float viscosity = 50.0f;
			float stretch = 5.0f;
			float stickiness = 20.0f;   // the solver's film drag: how much a thin smear holds where a glob runs (10-17,
			                            // "sticky liquid, not a gel"; it was the grip, which held everything still then let go)
			float friction = 60.0f;     // the solver's sliding friction
			float slime = 3.0f;         // how fast a slime string gives way (lower: longer strings, tests/fluid 18 cm at 3)
			int strings = 24;           // slime strings at once
			float overflow = 0.5f;      // inside her: this share of each pulse backs out around his shaft at once
			float lifetime = 45.0f;     // seconds a particle lasts
			float fade = 6.0f;          // its last seconds it thins away
			int maxParticles = 1024;
			float capacity[3] = { 150.0f, 110.0f, 80.0f };   // particles a vagina, an anus, a mouth holds before it spills
			float reservoirLife = 90.0f;   // seconds a reservoir leaks at most
			float moveGain = 1.5f;      // leaking faster per m/s the opening moves (she stands, walks)
			float seep = 3000.0f;       // her canal's hold on what is left in it after the gush: higher, it seeps slower
			float swallow = 0.5f;       // the chance a mouth swallows most of it when he pulls out (else it runs out)
			bool collide = true;
			float shaftRadius = 1.0f;   // units: his shaft as a capsule
			float bodyScale = 1.0f;     // the body capsules' radii x this (Colliders)
			float dropScale = 1.8f;     // a drop drawn this much larger than its volume (only drawn: the physics keeps it)
			float blobScale = 1.25f;    // a blob drawn this much larger about its middle
		} P;

		// BSModelDB::Demand as Strands.cpp calls it (AE 2275154)
		struct ModelArgs
		{
			std::int32_t lodFadeMult{ 0 };
			std::uint32_t loadLevel{ 0 };
			std::uint8_t flags{ 0x01 | 0x08 | 0x20 };
			std::uint8_t pad[3]{};
		};
		static_assert(sizeof(ModelArgs) == 0xC);
		using Demand_t = std::uint32_t (*)(const char*, RE::NiPointer<NiNode>*, const ModelArgs&);
		constexpr Hook::IdPair kDemand{ 1225688, 2275154 };

		std::mutex lock;
		std::uint32_t rng = 0x9E3779B9u;
		float Random01()
		{
			rng = rng * 1664525u + 1013904223u;
			return (float)((rng >> 8) & 0xFFFF) / 65535.0f;
		}

		float Dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		V Cross(const V& a, const V& b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
		float Length(const V& v) { return std::sqrt(Dot(v, v)); }
		V Unit(const V& v, const V& fb = V(1, 0, 0))
		{
			const float l = Length(v);
			return l > 1e-6f ? v * (1.0f / l) : fb;
		}

		// the engine keeps an object's axes in the rows: world = pos + rot^T (local * scale). So a rotation M that
		// takes a local direction to a world one (M d = n) is stored transposed
		void StoreRotation(NiTransform& t, const float m[3][3])
		{
			for (int r = 0; r < 3; r++)
				for (int c = 0; c < 3; c++)
					t.rot.data[r][c] = m[c][r];
		}

		// the shortest rotation taking unit a_from onto unit a_to
		void ArcRotation(const V& a_from, const V& a_to, float m[3][3])
		{
			const float c = Dot(a_from, a_to);
			if (c < -0.999f) {
				V axis = std::fabs(a_from.x) < 0.9f ? V(1, 0, 0) : V(0, 1, 0);
				axis = Unit(axis - a_from * Dot(axis, a_from), V(0, 0, 1));
				const float a[3]{ axis.x, axis.y, axis.z };
				for (int r = 0; r < 3; r++)
					for (int k = 0; k < 3; k++)
						m[r][k] = 2.0f * a[r] * a[k] - (r == k ? 1.0f : 0.0f);
				return;
			}
			const V v = Cross(a_from, a_to);
			const float k = 1.0f / (1.0f + c);
			m[0][0] = 1.0f - k * (v.y * v.y + v.z * v.z);
			m[0][1] = -v.z + k * v.x * v.y;
			m[0][2] = v.y + k * v.x * v.z;
			m[1][0] = v.z + k * v.x * v.y;
			m[1][1] = 1.0f - k * (v.x * v.x + v.z * v.z);
			m[1][2] = -v.x + k * v.y * v.z;
			m[2][0] = -v.y + k * v.x * v.z;
			m[2][1] = v.x + k * v.y * v.z;
			m[2][2] = 1.0f - k * (v.x * v.x + v.y * v.y);
		}

		// a world transform whose local X runs along dir (the rope's axis), as Strands.cpp's Frame
		NiTransform Frame(const V& at, const V& dir, float scale)
		{
			const V x = Unit(dir);
			const V up = std::fabs(x.z) < 0.9f ? V(0, 0, 1) : V(0, 1, 0);
			const V y = Unit(Cross(up, x));
			const V z = Cross(x, y);
			NiTransform t;
			const V rows[3] = { x, y, z };
			for (int r = 0; r < 3; r++) {
				t.rot.data[r][0] = rows[r].x;
				t.rot.data[r][1] = rows[r].y;
				t.rot.data[r][2] = rows[r].z;
			}
			t.pos = at;
			t.scale = scale;
			return t;
		}

		void Place(NiAVObject* a_obj, const NiTransform& w, const NiTransform& pw)
		{
			NiTransform& lt = G::Local(a_obj);
			lt.rot = w.rot * pw.rot.Transpose();
			lt.pos = (pw.rot * (w.pos - pw.pos)) * (1.0f / pw.scale);
			lt.scale = w.scale / pw.scale;
			G::World(a_obj) = w;
		}

		V Stretch(const V& a_value, const V& a_axis, float a_factor)   // volume kept: longer along a_axis, thinner across
		{
			const float side = 1.0f / std::sqrt(a_factor);
			return a_value * side + a_axis * (Dot(a_value, a_axis) * (a_factor - side));
		}

		Actor* ActorOf(UInt32 formID)
		{
			if (!formID)
				return nullptr;
			for (auto& e : actorEntries)
				if (e.actor && e.actor->formID == formID)
					return e.actor;
			return nullptr;
		}

		NiNode* RootOf(UInt32 formID)
		{
			Actor* a = ActorOf(formID);
			return a ? G::AsNode(G::Root(a)) : nullptr;
		}

		// his shaft: tip and axis (Aim's chain, root to tip)
		bool TipOf(UInt32 formID, V& a_tip, V& a_axis)
		{
			Actor* a = ActorOf(formID);
			std::vector<NiAVObject*> chain;
			if (!a || !AimChain(a, chain) || chain.size() < 2)
				return false;
			a_tip = G::World(chain.back()).pos;
			a_axis = Unit(a_tip - G::World(chain[chain.size() - 2]).pos);
			return true;
		}

		// ---- openings: Aim's entrance and inward axis, kept in her Pelvis_skin's (a mouth: her head's) frame, so a
		// reservoir still finds it after the scene, when Aim no longer lists it
		struct OpeningRef
		{
			UInt32 owner = 0;
			int kind = 0;
			RE::NiPointer<NiAVObject> node;
			V local, inLocal;
		};
		std::vector<OpeningRef> openings;

		NiAVObject* OpeningNode(Actor* a_her, int a_kind)
		{
			NiAVObject* root = G::Root(a_her);
			if (!root)
				return nullptr;
			static RE::BSFixedString pelvis("Pelvis_skin"), head("Head");
			return G::SkeletonNode(root, a_kind == 2 ? head : pelvis);
		}

		void RefreshOpenings()
		{
			for (const AimOpening& o : AimOpenings()) {
				if (o.kind < 0 || o.kind > 2)
					continue;
				Actor* her = ActorOf(o.owner);
				NiAVObject* node = her ? OpeningNode(her, o.kind) : nullptr;
				if (!node)
					continue;
				auto it = std::find_if(openings.begin(), openings.end(), [&](const OpeningRef& r) { return r.owner == o.owner && r.kind == o.kind; });
				if (it == openings.end()) {
					if (openings.size() >= 32)
						openings.erase(openings.begin());
					openings.push_back(OpeningRef{ o.owner, o.kind });
					it = std::prev(openings.end());
				}
				const NiTransform& w = G::World(node);
				it->node = RE::NiPointer<NiAVObject>(node);
				it->local = (w.rot * (o.point - w.pos)) * (1.0f / w.scale);
				it->inLocal = w.rot * Unit(o.in);
			}
		}

		bool OpeningNow(UInt32 a_owner, int a_kind, V& a_center, V& a_inward)
		{
			for (const OpeningRef& r : openings)
				if (r.owner == a_owner && r.kind == a_kind && r.node && r.node->parent) {
					const NiTransform& w = G::World(r.node.get());
					a_center = w.pos + w.rot.Transpose() * (r.local * w.scale);
					a_inward = Unit(w.rot.Transpose() * r.inLocal);
					return true;
				}
			return false;
		}

		bool TipInside(UInt32 a_plug, const V& a_center, const V& a_inward)
		{
			V tip, axis;
			if (!TipOf(a_plug, tip, axis))
				return false;
			const V d = tip - a_center;
			const float depth = Dot(d, a_inward);
			return depth > 0.0f && depth < kPlugDepth && Length(d - a_inward * depth) < kPlugLateral;
		}

		// ---- the meshes: pools of slots, one Demand prototype each
		struct Slot
		{
			RE::NiPointer<NiNode> root;
			std::vector<NiAVObject*> bones;
			NiNode* attachedTo = nullptr;
			bool failed = false;
			bool used = false;              // drawn this frame
			bool live = false;              // a blob wrapped around particles (its seed and surface mean something)
			std::uint32_t domain = 0;       // whose particles it drew last
			std::size_t seed = kNone;
			std::vector<SurfacePoint> surface;
		};
		std::array<Slot, kBlobPool> blobs;
		std::array<Slot, kRopePool> ropes;
		std::vector<V> directions;          // the blob mesh's bones' directions (its vertices, a unit sphere)

		bool Load(Slot& s, const char* a_path, const char* a_prefix, int a_max)
		{
			const auto fn = Hook::Resolve(kDemand);
			if (!fn) {
				G::Once("fluid|nodemand", "[fluid] BSModelDB::Demand not resolved on this executable; fluid off");
				return false;
			}
			RE::NiPointer<NiNode> model;
			const ModelArgs args{};
			const auto error = reinterpret_cast<Demand_t>(*fn)(a_path, &model, args);
			if (error != 0 || !model) {
				G::Once(std::string("fluid|nomesh|") + a_path, "[fluid] could not load meshes\\{} (error {}): that slot stays off", a_path, error);
				return false;
			}
			s.bones.clear();
			for (int i = 0; i < a_max; i++) {
				char name[24];
				std::snprintf(name, sizeof(name), "%s%02d", a_prefix, i);
				NiAVObject* b = G::Find(model.get(), name);
				if (!b)
					break;
				s.bones.push_back(b);
			}
			if (s.bones.size() < 4) {
				spdlog::info("[fluid] meshes\\{} has {} {} bones: that slot stays off", a_path, s.bones.size(), a_prefix);
				return false;
			}
			NiTransform identity;   // spelt out: NiTransform() zero-fills its matrix (Strands.cpp)
			identity.rot.data[0][0] = identity.rot.data[1][1] = identity.rot.data[2][2] = 1.0f;
			G::Local(model.get()) = identity;
			s.root = model;
			return true;
		}

		bool LoadBlob(int i)
		{
			Slot& s = blobs[i];
			if (s.root || s.failed)
				return (bool)s.root;
			char path[64];
			std::snprintf(path, sizeof(path), "Anatomy\\Fluid\\Blob%02d.nif", i);
			if (!Load(s, path, "Blob", 200)) {
				s.failed = true;
				return false;
			}
			if (directions.empty()) {
				for (NiAVObject* b : s.bones)
					directions.push_back(Unit(G::Local(b).pos, V(0, 0, 1)));
				spdlog::info("[fluid] a blob mesh with {} points", directions.size());
			}
			if (s.bones.size() != directions.size()) {
				spdlog::info("[fluid] meshes\\{} has {} points, the first blob {}: that slot stays off", path, s.bones.size(), directions.size());
				s.root.reset();
				s.failed = true;
				return false;
			}
			return true;
		}

		bool LoadRope(int i)
		{
			Slot& s = ropes[i];
			if (s.root || s.failed)
				return (bool)s.root;
			char path[64];
			std::snprintf(path, sizeof(path), "Anatomy\\Fluid\\Rope%02d.nif", i);
			if (!Load(s, path, "Strand", kMaxRopeBones)) {
				s.failed = true;
				return false;
			}
			return true;
		}

		void Detach(Slot& s)
		{
			if (s.root && s.root->parent)
				s.root->parent->DetachChild(s.root.get());
			s.attachedTo = nullptr;
		}

		// under an actor's 3D, every shape attached again by itself, or nothing draws (Strands.cpp, tests 13-20)
		void Attach(Slot& s, NiNode* a_parent)
		{
			if (s.attachedTo == a_parent && s.root->parent == a_parent)
				return;
			Detach(s);
			a_parent->AttachChild(s.root.get(), true);
			for (UInt16 i = 0; i < G::ChildCount(s.root.get()); i++) {
				NiAVObject* c = G::Child(s.root.get(), i);
				if (c && !G::AsNode(c)) {
					RE::NiPointer<NiAVObject> keep(c);
					s.root->DetachChild(c);
					s.root->AttachChild(c, true);
				}
			}
			s.attachedTo = a_parent;
		}

		void Hide(Slot& s)
		{
			if (s.root)
				s.root->flags.flags |= 1ull;   // app-culled
		}

		void Show(Slot& s)
		{
			if (s.root)
				s.root->flags.flags &= ~1ull;
		}

		// the world bound by hand around the posed points: the shape's model bound is around its rest pose
		void Finish(Slot& s, const NiTransform& pw, const V& lo, const V& hi)
		{
			G::World(s.root.get()) = pw;
			RE::NiUpdateData update{};
			s.root->UpdateDownwardPass(update, 0);
			const V c = (lo + hi) * 0.5f;
			const float br = Length(hi - lo) * 0.5f + 1.0f;
			for (UInt16 i = 0; i < G::ChildCount(s.root.get()); i++)
				if (NiAVObject* sh = G::Child(s.root.get(), i); sh && !G::AsNode(sh)) {
					sh->worldBound.center.x = c.x, sh->worldBound.center.y = c.y, sh->worldBound.center.z = c.z;
					sh->worldBound.fRadius = br;
				}
			s.root->worldBound.center.x = c.x, s.root->worldBound.center.y = c.y, s.root->worldBound.center.z = c.z;
			s.root->worldBound.fRadius = br;
			Show(s);
		}

		// ---- domains: a floor, the people around it, their particles
		struct Domain
		{
			std::uint32_t id = 0;
			V origin;                        // world: the floor under the scene
			std::vector<UInt32> hosts;
			UInt32 parentId = 0;             // whose 3D our meshes hang under
			NiNode* parent = nullptr;
			std::vector<FluidSim::Particle> particles;
			std::vector<FluidSim::Anchor> anchors;
			std::vector<std::pair<UInt32, int>> holes;   // openings particles may enter
			std::vector<FluidSim::Capsule> capsules;
			std::vector<UInt32> capsuleOwners;           // whose body each capsule is
			std::unordered_map<UInt32, float> dressedFor;   // a woman's seconds without our genitals shape (dressed)
			std::vector<FluidSim::Hole> holeShapes;
			std::vector<FluidSim::Canal> canals;         // one per entry of holes, in its order (particles keep the index)
			std::vector<float> canalRadius, canalOut;    // its radius now (units), seconds he has been out of it
			std::vector<char> canalPlugged;
			std::unordered_map<std::string, V> previous; // a collider's last world position: its velocity
			FluidSim::Solver solver{ kSpacing };
			float accumulator = 0.0f;
			float idle = 0.0f;               // seconds with nothing in it
			float stat = 0.0f;
			std::size_t lastCount = (std::size_t)-1;
			bool listed = false;             // its colliders logged once
		};
		std::vector<std::unique_ptr<Domain>> domains;
		bool collidersListed = false;   // the first pool's colliders, once a session
		std::uint32_t nextId = 0;

		V ToLocal(const Domain& d, const V& p) { return (p - d.origin) * (1.0f / kUnitsPerMeter); }
		V ToWorld(const Domain& d, const V& p) { return d.origin + p * kUnitsPerMeter; }

		Domain* FindDomain(std::uint32_t id)
		{
			for (auto& d : domains)
				if (d->id == id)
					return d.get();
			return nullptr;
		}

		void AddHost(Domain& d, UInt32 id)
		{
			if (id && std::find(d.hosts.begin(), d.hosts.end(), id) == d.hosts.end())
				d.hosts.push_back(id);
		}

		void Release(Domain& d)
		{
			for (Slot& s : blobs)
				if (s.domain == d.id)
					s.live = false, s.domain = 0, Hide(s), Detach(s);
			for (Slot& s : ropes)
				if (s.domain == d.id)
					s.domain = 0, Hide(s), Detach(s);
		}

		// the domain at a_point for these people: an open one near enough on the same floor, or a new one whose floor is
		// the lowest one's feet (their 3D root)
		Domain* DomainFor(const std::vector<UInt32>& a_hosts, const V& a_point)
		{
			float ground = a_point.z;
			UInt32 parentId = 0;
			NiNode* parent = nullptr;
			for (UInt32 id : a_hosts) {
				NiNode* root = RootOf(id);
				if (!root)
					continue;
				ground = (std::min)(ground, G::World(root).pos.z);
				// under someone else's 3D than the player's when there is one: the player's is replaced on every
				// first/third person switch, and the pool would go with it (OStrands' Attach, the same choice)
				if (!parent || (parentId == 0x14 && id != 0x14))
					parent = root, parentId = id;
			}
			if (!parent)
				return nullptr;
			for (auto& d : domains) {
				const V off = a_point - d->origin;
				if (std::fabs(d->origin.z - ground) < kDomainStep && std::sqrt(off.x * off.x + off.y * off.y) < kDomainReach) {
					for (UInt32 id : a_hosts)
						AddHost(*d, id);
					return d.get();
				}
			}
			if (domains.size() >= kMaxDomains) {
				Release(*domains.front());
				domains.erase(domains.begin());
			}
			auto d = std::make_unique<Domain>();
			d->id = ++nextId;
			d->origin = V(a_point.x, a_point.y, ground);
			d->hosts = a_hosts;
			d->parentId = parentId;
			d->parent = parent;
			spdlog::info("[fluid] pool {} opened, floor {:.1f} units below", d->id, a_point.z - ground);
			domains.push_back(std::move(d));
			return domains.back().get();
		}

		// ---- climax: emitters (his pulses) and reservoirs (what an opening holds)
		struct Pulse
		{
			float at = 0.0f, speed = 0.0f, spacing = 0.0f;
			int particles = 0;
			float spread = 0.0f, angle = 0.0f, wave = 0.0f, frequency = 0.0f;
		};

		struct Emitter
		{
			std::uint32_t id = 0;
			UInt32 him = 0, other = 0, her = 0;
			int kind = -1;
			std::vector<Pulse> pulses;
			std::size_t pulse = 0;
			int emitted = 0, count = 0;
			float slot = 0.0f, clock = 0.0f, hang = 0.0f;
			bool oozing = false, inside = false, hasNozzle = false;
			std::uint32_t run = 0, domain = 0;
			V lastNozzle, carried;
		};
		std::vector<Emitter> emitters;

		struct Reservoir
		{
			std::uint32_t id = 0;
			UInt32 her = 0, plug = 0;
			int kind = 0;
			std::vector<UInt32> hosts;
			float volume = 0.0f, gush = 0.0f, release = 0.0f, age = 0.0f, lost = 0.0f, out = 0.0f, dressed = 0.0f;
			bool plugged = true;
			std::uint32_t domain = 0, run = 0;
			int glob = 0;
			float globSpeed = 1.0f, phase = 0.0f, period = 2.0f;
			V lastExit;
			bool hasExit = false;
		};
		std::vector<Reservoir> reservoirs;

		struct Queued
		{
			UInt32 him, other, her;
			int kind;
		};
		std::mutex queueLock;   // Strands' tick queues (the same thread today; Papyrus may later)
		std::vector<Queued> queue;
		std::uint32_t nextEmitter = 0, nextRun = 0;
		LARGE_INTEGER lastTick{};
		float clock = 0.0f;   // seconds of fluid ticks this session
		// a mouth's state for faces (MouthState): her formID -> (state, until the clock)
		std::mutex mouthLock;
		std::unordered_map<UInt32, std::pair<int, float>> mouths;
		std::unordered_map<UInt32, int> mouthsSent;   // what Rapport was last told (RFAM)

		void SendMouths()
		{
			std::vector<std::pair<UInt32, int>> changed;
			{
				std::lock_guard<std::mutex> l(mouthLock);
				for (auto it = mouths.begin(); it != mouths.end();) {
					const int now = it->second.second > clock ? it->second.first : 0;
					int& sent = mouthsSent[it->first];
					if (now != sent) {
						sent = now;
						changed.emplace_back(it->first, now);
					}
					if (now == 0) {
						mouthsSent.erase(it->first);
						it = mouths.erase(it);
					} else
						++it;
				}
			}
			auto* messaging = changed.empty() ? nullptr : F4SE::GetMessagingInterface();
			for (const auto& [id, state] : changed) {
				if (messaging) {
					FaceAuthority::MouthFluidMessage m{ 1, id, (std::uint32_t)state };
					messaging->Dispatch(FaceAuthority::kMouthFluid, &m, sizeof(m), nullptr);   // to everyone: Rapport keeps ours
				}
				spdlog::info("[fluid] {:08X}'s mouth: state {}", id, state);
			}
		}
		void SetMouth(UInt32 a_her, int a_state, float a_for)
		{
			std::lock_guard<std::mutex> l(mouthLock);
			auto& m = mouths[a_her];
			if (m.first == 3 && m.second > clock && a_state != 3)
				return;   // a swallow plays out
			m = { a_state, clock + a_for };
		}
		int logged = 0;

		template <class... Args>
		void Note(spdlog::format_string_t<Args...> a_fmt, Args&&... a_args)
		{
			if (logged >= 200)
				return;
			logged++;
			spdlog::info(a_fmt, std::forward<Args>(a_args)...);
		}

		float Taper(float a_progress)   // a pulse slows toward its tail
		{
			const float t = std::clamp((a_progress - kTailStart) / (1.0f - kTailStart), 0.0f, 1.0f);
			return 1.0f - kTailSlowdown * t * t * (3.0f - 2.0f * t);
		}

		// OStrands' Plan: P.pulses pulses, each weaker and later than the last, some of them wavy, then a dribble
		void Plan(Emitter& e)
		{
			e.pulses.clear();
			float at = 0.0f;
			const int n = (std::max)(1, P.pulses);
			for (int k = 0; k < n; k++) {
				const float progress = n > 1 ? (float)k / (n - 1) : 0.0f;
				const float strength = (std::max)(0.15f, 1.0f - 0.85f * progress) * (0.85f + 0.3f * Random01());
				const bool wavy = Random01() < 0.6f;
				Pulse p;
				p.at = at;
				p.speed = (0.2f + 2.0f * strength) * P.strength;
				p.spacing = kSlotSpacing * kSpacing;
				p.particles = (std::max)(1, (int)std::lround(40.0f * strength * P.amount));
				p.spread = 0.03f + 0.06f * Random01();
				p.angle = Random01() * 2.0f * kPi;
				p.wave = wavy ? 0.03f + 0.09f * Random01() : 0.0f;
				p.frequency = 6.0f + 8.0f * Random01();
				e.pulses.push_back(p);
				at += 0.5f + 0.35f * progress + 0.15f * Random01();
			}
			Pulse dribble;
			dribble.at = at;
			dribble.speed = 0.08f;
			dribble.spacing = kDribbleSpacing * kSpacing;
			dribble.particles = (std::max)(1, (int)std::lround(8.0f * P.amount));
			dribble.spread = 0.1f;
			e.pulses.push_back(dribble);
		}

		Reservoir& ReservoirOf(UInt32 a_her, int a_kind)
		{
			for (Reservoir& r : reservoirs)
				if (r.her == a_her && r.kind == a_kind)
					return r;
			Reservoir r;
			r.id = ++nextEmitter;
			r.her = a_her;
			r.kind = a_kind;
			r.phase = Random01() * 2.0f * kPi;
			r.period = 1.4f + Random01();
			reservoirs.push_back(r);
			return reservoirs.back();
		}

		void Deposit(const Emitter& e, float a_particles)
		{
			if (!e.her || a_particles <= 0.0f)
				return;
			Reservoir& r = ReservoirOf(e.her, e.kind);
			r.plug = e.him;
			r.hosts = { e.her, e.him };
			r.volume += a_particles;
			r.plugged = true;
			r.age = 0.0f;
			r.lost = 0.0f;
		}

		// a new particle of a run: linked to the run's last one when they are close
		void Emit(Domain& d, const V& a_world, const V& a_velocity, std::uint32_t a_run, float a_spacing, std::size_t& a_newest)
		{
			FluidSim::Particle p;
			p.life = P.lifetime * (0.85f + 0.3f * Random01());
			p.pos = ToLocal(d, a_world);
			p.vel = a_velocity;
			p.run = a_run;
			auto& ps = d.particles;
			if (!ps.empty() && ps.back().run == a_run) {
				const float gap = Length(p.pos - ps.back().pos);
				if (gap < 2.0f * kSpacing)
					ps.back().link = (std::max)(a_spacing, gap);
			}
			a_newest = ps.size();
			ps.push_back(p);
		}

		void HoldBy(Domain& d, std::uint32_t a_owner, std::size_t a_newest, const V& a_world)
		{
			const V hold = ToLocal(d, a_world);
			auto held = std::find_if(d.anchors.begin(), d.anchors.end(), [&](const FluidSim::Anchor& a) { return a.owner == a_owner; });
			if (a_newest != kNone) {
				const float rest = (std::max)(Length(d.particles[a_newest].pos - hold), 0.5f * kSpacing);
				if (held != d.anchors.end())
					*held = FluidSim::Anchor{ a_newest, hold, rest, a_owner };
				else
					d.anchors.push_back(FluidSim::Anchor{ a_newest, hold, rest, a_owner });
			} else if (held != d.anchors.end())
				held->point = hold;
		}

		// OStrands' Step: the pulses due by now leave his tip, each a run of linked particles with a little sway and a
		// tapering tail; if he is inside her when a pulse starts, it goes into her reservoir but for P.overflow of it,
		// which oozes out of her entrance. False: done
		bool StepEmitter(Emitter& e, float dt)
		{
			if (e.pulses.empty())
				Plan(e);
			V tip, axis;
			if (!TipOf(e.him, tip, axis))
				return false;
			V center, inward;
			bool haveOpening = false;
			e.inside = false;
			if (e.her && OpeningNow(e.her, e.kind, center, inward)) {
				haveOpening = true;
				e.inside = Dot(tip - center, inward) > 0.0f;
			}
			const V nozzle = tip + axis * kNozzleOffset;
			if (e.hasNozzle && dt > 0.0f) {
				V measured = (nozzle - e.lastNozzle) * (1.0f / (dt * kUnitsPerMeter));
				if (const float s = Length(measured); s > kMaxNozzleSpeed)
					measured = measured * (kMaxNozzleSpeed / s);
				const float blend = 1.0f - std::exp(-dt / kCarrySmoothing);
				e.carried = e.carried + (measured - e.carried) * blend;
			}
			const V carried = e.carried * kCarry;
			e.lastNozzle = nozzle;
			e.hasNozzle = true;
			e.clock += dt;

			Domain* d = FindDomain(e.domain);
			if (!d) {
				d = DomainFor({ e.him, e.other, e.her }, nozzle);
				if (!d)
					return false;
				e.domain = d->id;
			}
			if (e.her && std::find(d->holes.begin(), d->holes.end(), std::make_pair(e.her, e.kind)) == d->holes.end())
				d->holes.emplace_back(e.her, e.kind);

			const V side = Unit(Cross(axis, V(0, 0, 1)), V(1, 0, 0));
			const V lift = Cross(side, axis);
			auto source = [&]() {
				if (!e.oozing || !haveOpening)
					return std::make_pair(nozzle, axis);
				return std::make_pair(center - inward * kExitDepth, inward * -1.0f);
			};
			const float now = e.clock;
			std::size_t newest = kNone;
			while (e.pulse < e.pulses.size()) {
				const Pulse& pulse = e.pulses[e.pulse];
				if (now < pulse.at)
					break;
				if (e.emitted == 0 && e.slot <= 0.0f) {
					e.run = ++nextRun;
					// inside her too it leaves his tip as particles: her canal holds them, and what does not fit backs
					// out around him (10-17; before, a reservoir counted it and oozed P.overflow at her entrance)
					e.oozing = false;
					e.count = pulse.particles;
				}
				const auto [origin, outlet] = source();
				const bool oozing = e.oozing && haveOpening;
				const float spacing = oozing ? kDribbleSpacing * kSpacing : pulse.spacing;
				const float elapsed = now - pulse.at;
				const float tilt = oozing ? 0.0f : std::tan(pulse.spread);
				const V aim = Unit(outlet + (side * std::cos(pulse.angle) + lift * std::sin(pulse.angle)) * tilt, outlet);
				while (e.emitted < e.count && e.slot <= elapsed) {
					const float speed = oozing ? kOozeSpeed : pulse.speed * Taper((float)e.emitted / (float)e.count);
					const float age = elapsed - e.slot;
					e.slot += spacing / (std::max)(speed, 0.02f);
					e.emitted++;
					if ((int)d->particles.size() >= P.maxParticles)
						continue;
					const float sway = oozing ? 0.0f : pulse.wave * std::sin(2.0f * kPi * pulse.frequency * (e.slot - age));
					const V bend = (lift * std::cos(pulse.angle) - side * std::sin(pulse.angle)) * sway;
					const V wobble = (side * (Random01() - 0.5f) + lift * (Random01() - 0.5f)) * (oozing ? 0.3f : 0.03f);
					const V dir = Unit(aim + bend + wobble, aim);
					const V vel = dir * speed + carried * (oozing ? 0.5f : 1.0f);
					const float turn = Random01() * 2.0f * kPi;
					const V disc = oozing ? (side * std::cos(turn) + lift * std::sin(turn)) * (kGlobDisc * kSpacing * kUnitsPerMeter * std::sqrt(Random01()))
					                      : V(0, 0, 0);
					Emit(*d, origin + disc + vel * (age * kUnitsPerMeter), vel, e.run, spacing, newest);
				}
				if (e.emitted < e.count)
					break;
				e.pulse++;
				e.emitted = 0;
				e.slot = 0.0f;
			}
			HoldBy(*d, e.id, newest, source().first);
			if (e.pulse < e.pulses.size())
				return true;
			e.hang += dt;
			const bool holding = std::any_of(d->anchors.begin(), d->anchors.end(), [&](const FluidSim::Anchor& a) { return a.owner == e.id; });
			if (holding && e.hang < kHangTime)
				return true;
			std::erase_if(d->anchors, [&](const FluidSim::Anchor& a) { return a.owner == e.id; });
			return false;
		}

		// OStrands' Leak: while he is in her, a slow seep (and what is past her capacity spills around his shaft); when he
		// pulls out, a gush, then globs at a rate by what is left, how much the opening faces down and how fast it moves;
		// a mouth swallows (P.swallow) or lets it run out. False: done
		bool Leak(Reservoir& r, float dt)
		{
			r.age += dt;
			V center, inward;
			if (!OpeningNow(r.her, r.kind, center, inward)) {
				r.lost += dt;
				return r.lost < kLostOpening && r.age < P.reservoirLife;
			}
			r.lost = 0.0f;
			// dressed: the body slot's armour replaces the naked body, our genitals shape with it, and what would leak
			// would come out through her clothes (in game 10-17: AAF dresses her the moment the scene ends). As
			// OStrands, it stops kDressedGrace seconds after. A mouth is never covered
			if (r.kind != 2) {
				Actor* her = ActorOf(r.her);
				static const RE::BSFixedString genitals("AnatomyGenitals");
				const bool bare = her && !actorUtils::IsActorMale(her) && G::Root(her) && G::Find(G::Root(her), genitals.c_str());
				r.dressed = bare || (her && actorUtils::IsActorMale(her)) ? 0.0f : r.dressed + dt;
				if (r.dressed > kDressedGrace) {
					Note("[fluid] {:08X} is dressed: her opening {} stops, {:.0f} left", r.her, r.kind, r.volume);
					return false;
				}
			}
			bool holding = false;
			for (auto& d : domains)
				for (const FluidSim::Anchor& a : d->anchors)
					holding = holding || a.owner == r.id;
			const bool empty = r.volume < 0.5f && r.gush < 0.5f;
			if (r.age > P.reservoirLife || (empty && !holding)) {
				Note("[fluid] {:08X}'s opening {} stops leaking, {:.0f} left", r.her, r.kind, r.volume);
				return false;
			}
			const V outward = inward * -1.0f;
			const V exit = center + outward * kExitDepth;
			// in her: Aim's lock (his shaft in this opening this frame), else his tip past her entrance near her axis.
			// The first test run (10-17) read every pulse as a pull-out: in Cowgirl his tip sat off the strict cone
			const bool aim = r.kind == 2 ? AimOralPartner(r.her) == r.plug
			                             : (((AimReceivedKinds(r.her) >> r.kind) & 1u) && AimPartner(r.her) == r.plug);
			r.out = aim || TipInside(r.plug, center, inward) ? 0.0f : r.out + dt;
			const bool plugged = r.out < kUnplugAfter;
			if (r.plugged && !plugged && r.volume >= 1.0f) {
				if (r.kind == 2 && Random01() < P.swallow) {
					const float gulp = r.volume * 0.9f;
					r.volume -= gulp;
					Note("[fluid] {:08X} swallows {:.0f}, {:.0f} left at her lips", r.her, gulp, r.volume);
					SetMouth(r.her, 3, 1.5f);
				}
				const float gush = r.volume * kGushShare;
				r.gush += gush;
				r.volume -= gush;
				Note("[fluid] {:08X} pulls out of {:08X}'s opening {}: {:.0f} gush, {:.0f} inside", r.plug, r.her, r.kind, gush, r.volume);
			}
			r.plugged = plugged;
			if (r.kind == 2 && (r.volume >= 1.0f || r.gush >= 1.0f))
				SetMouth(r.her, plugged ? 1 : 2, 0.3f);   // refreshed every frame while it lasts

			Domain* d = FindDomain(r.domain);
			if (!d) {
				d = DomainFor(r.hosts.empty() ? std::vector<UInt32>{ r.her } : r.hosts, exit);
				if (!d)
					return false;
				r.domain = d->id;
			}
			if (std::find(d->holes.begin(), d->holes.end(), std::make_pair(r.her, r.kind)) == d->holes.end())
				d->holes.emplace_back(r.her, r.kind);

			V carried(0, 0, 0);
			if (r.hasExit && dt > 0.0f) {
				carried = (exit - r.lastExit) * (1.0f / (dt * kUnitsPerMeter));
				if (const float s = Length(carried); s > kMaxNozzleSpeed)
					carried = carried * (kMaxNozzleSpeed / s);
			}
			r.lastExit = exit;
			r.hasExit = true;
			const float facing = std::clamp(0.5f - outward.z, 0.05f, 1.0f);   // facing up (she lies on her back): little
			const float wave = (std::max)(0.0f, std::sin(2.0f * kPi * r.age / r.period + r.phase));
			const float pulse = 0.25f + 1.5f * wave * wave;
			const float moving = 1.0f + P.moveGain * (std::min)(Length(carried), 1.0f);
			const float capacity = P.capacity[std::clamp(r.kind, 0, 2)];
			float rate = (plugged ? r.volume * kSeep : r.volume * kLeak * facing * moving) * pulse;
			if (plugged && r.volume > capacity)
				rate += (r.volume - capacity) * 4.0f;   // more than she holds: it backs out around him
			if (!plugged && r.gush > 0.0f)
				rate += kGushRate * (std::max)(facing, 0.3f);
			r.release += rate * dt;

			const V side = Unit(Cross(outward, V(0, 0, 1)), V(1, 0, 0));
			const V lift = Cross(side, outward);
			std::size_t newest = kNone;
			while (r.release >= 1.0f && (r.volume >= 1.0f || r.gush >= 1.0f)) {
				r.release -= 1.0f;
				if (r.gush >= 1.0f)
					r.gush -= 1.0f;
				else
					r.volume -= 1.0f;
				if ((int)d->particles.size() >= P.maxParticles)
					continue;
				if (r.glob <= 0) {
					r.run = ++nextRun;
					r.glob = kGlobMin + (int)(Random01() * (kGlobMax - kGlobMin));
					r.globSpeed = 0.5f + 0.8f * Random01();
				}
				r.glob--;
				const V wobble = (side * (Random01() - 0.5f) + lift * (Random01() - 0.5f)) * 0.4f;
				const V vel = Unit(outward + wobble, outward) * (kLeakSpeed * r.globSpeed) + carried * 0.5f;
				const float turn = Random01() * 2.0f * kPi;
				const V disc = (side * std::cos(turn) + lift * std::sin(turn)) * (kGlobDisc * kSpacing * kUnitsPerMeter * std::sqrt(Random01()));
				Emit(*d, exit + disc + vel * (Random01() * dt * kUnitsPerMeter), vel, r.run, kDribbleSpacing * kSpacing, newest);
			}
			if (r.volume < 1.0f && r.gush < 1.0f)
				r.release = 0.0f;
			HoldBy(*d, r.id, newest, exit);
			return true;
		}

		// ---- simulation

		void AddCollider(Domain& d, UInt32 a_owner, const std::string& a_key, const V& a, const V& b, float a_radius, float dt)
		{
			d.capsuleOwners.push_back(a_owner);
			const V la = ToLocal(d, a), lb = ToLocal(d, b);
			V va(0, 0, 0), vb(0, 0, 0);
			auto pa = d.previous.find(a_key + "a"), pb = d.previous.find(a_key + "b");
			if (pa != d.previous.end() && dt > 0.0f)
				va = (la - pa->second) * (1.0f / dt);
			if (pb != d.previous.end() && dt > 0.0f)
				vb = (lb - pb->second) * (1.0f / dt);
			if (Length(va) > 6.0f)   // a teleport, not a motion
				va = V(0, 0, 0);
			if (Length(vb) > 6.0f)
				vb = V(0, 0, 0);
			d.previous[a_key + "a"] = la;
			d.previous[a_key + "b"] = lb;
			d.capsules.push_back(FluidSim::Capsule{ la, lb, a_radius / kUnitsPerMeter, va, vb });
		}

		// her canals, one per opening the domain knows: from her entrance inward, rebuilt every frame (it moves with her).
		// Plugged (Aim's lock on his shaft in it, held kUnplugAfter): as wide as his shaft and kCanalAround spacings; out:
		// it closes to kCanalShut at kCanalClose, squeezing out what no longer fits. An opening not found this frame keeps
		// its last canal, still: particles hold their canal's index
		void Canals(Domain& d, float dt)
		{
			const std::size_t n = d.holes.size();
			d.canals.resize(n);
			d.canalRadius.resize(n, 0.0f);
			d.canalOut.resize(n, 1e9f);
			d.canalPlugged.resize(n, 0);
			for (std::size_t k = 0; k < n; k++) {
				const auto [owner, kind] = d.holes[k];
				const int kk = std::clamp(kind, 0, 2);
				FluidSim::Canal& c = d.canals[k];
				V center, inward;
				if (!OpeningNow(owner, kind, center, inward)) {
					c.va = c.vb = V(0, 0, 0);
					continue;
				}
				// at least as deep as his tip is in her: his pulses leave it, and what leaves it must start inside
				float depth = kCanalDepth[kk];
				V tip, axis;
				const UInt32 him = kind == 2 ? AimOralPartner(owner) : AimPartner(owner);
				if (him && TipOf(him, tip, axis))
					depth = (std::max)(depth, (std::min)(Dot(tip - center, inward) + 1.5f, 2.5f * kCanalDepth[kk]));
				const V a = ToLocal(d, center), b = ToLocal(d, center + inward * depth);
				const bool fresh = d.canalRadius[k] <= 0.0f;
				c.va = fresh || dt <= 0.0f ? V(0, 0, 0) : (a - c.a) * (1.0f / dt);
				c.vb = fresh || dt <= 0.0f ? V(0, 0, 0) : (b - c.b) * (1.0f / dt);
				if (Length(c.va) > 6.0f || Length(c.vb) > 6.0f)   // a teleport
					c.va = c.vb = V(0, 0, 0);
				c.a = a, c.b = b;
				const bool held = kind == 2 ? AimOralPartner(owner) != 0 : ((AimReceivedKinds(owner) >> kind) & 1u) != 0;
				d.canalOut[k] = held ? 0.0f : d.canalOut[k] + dt;
				d.canalPlugged[k] = d.canalOut[k] < kUnplugAfter;
				const float open = P.shaftRadius + kCanalAround * kSpacing * kUnitsPerMeter;
				float& r = d.canalRadius[k];
				if (d.canalPlugged[k] || fresh)
					r = d.canalPlugged[k] ? open : kCanalShut[kk];
				else
					r = (std::max)(kCanalShut[kk], r - kCanalClose * dt);
				c.r = r / kUnitsPerMeter;
			}
		}

		// what is in each canal: a mouth tells faces (Rapport) full / running out, and swallows most of it as he pulls out
		void CanalStates(Domain& d)
		{
			for (std::size_t k = 0; k < d.canals.size(); k++) {
				const auto [owner, kind] = d.holes[k];
				int in = 0;
				for (const FluidSim::Particle& p : d.particles)
					in += p.canal == (int)k;
				const bool plugged = d.canalPlugged[k] != 0;
				if (kind == 2 && in > 0) {
					if (!plugged && d.canalOut[k] >= kUnplugAfter && d.canalOut[k] < kUnplugAfter + 0.05f && Random01() < P.swallow) {
						int gulp = 0;
						for (FluidSim::Particle& p : d.particles)
							if (p.canal == (int)k && Random01() < 0.9f)
								p.life = 0.0f, gulp++;
						Note("[fluid] {:08X} swallows {}, {} left at her lips", owner, gulp, in - gulp);
						SetMouth(owner, 3, 1.5f);
					} else
						SetMouth(owner, plugged ? 1 : 2, 0.3f);
				}
			}
		}

		void Colliders(Domain& d, float dt)
		{
			d.capsules.clear();
			d.capsuleOwners.clear();
			d.holeShapes.clear();
			if (P.collide) {
				for (const Collision& c : otherColliders) {
					if (c.isProp || !c.colliderActor)
						continue;
					const UInt32 id = c.colliderActor->formID;
					if (std::find(d.hosts.begin(), d.hosts.end(), id) == d.hosts.end())
						continue;
					for (std::size_t k = 0; k < c.collisionSpheres.size(); k++) {
						const Sphere& sp = c.collisionSpheres[k];
						char key[96];
						std::snprintf(key, sizeof(key), "%08X|%s|%zu", id, sp.NodeName.c_str(), k);
						AddCollider(d, id, key, sp.worldPos, sp.worldPos, (float)sp.radius, dt);
						if (!d.listed && !collidersListed)
							Note("[fluid] pool {} collider {:08X} {} r {:.1f}", d.id, id, sp.NodeName, sp.radius);
					}
				}
				d.listed = d.listed || !d.capsules.empty();
				collidersListed = collidersListed || d.listed;
				// the body itself, as capsules along the skeleton: OCBPCollisionConfig holds only what the physics needs
				// (breasts, butt, hands), so a first test run (10-17) let everything fall through both of them to the
				// floor. Radii in units for a scale-1 body: the median distance of the skin from each segment, measured on
				// the game's built women's (Anatomy 3BBB) and men's (BodyTalk) bodies (fo4-anatomy
				// studies/fluid_body_capsules.py, 10-17): half the skin is outside the capsule, so fluid sits ON it. The
				// lower belly takes the 25th percentile (7.5 / 4.0): its median (10.4 / 8.1) wrapped the crotch, and fluid
				// leaving her opening was pushed away from it to the capsule's edge
				struct BodyPart
				{
					const char *a, *b;
					float woman, man;
				};
				static const BodyPart kParts[] = {
					{ "SPINE1", "Chest", 7.3f, 9.3f }, { "Pelvis", "SPINE1", 7.5f, 4.0f },
					{ "LLeg_Thigh", "LLeg_Calf", 6.2f, 6.4f }, { "RLeg_Thigh", "RLeg_Calf", 6.2f, 6.4f },
					{ "LLeg_Calf", "LLeg_Foot", 3.4f, 3.7f }, { "RLeg_Calf", "RLeg_Foot", 3.4f, 3.7f },
					{ "LArm_UpperArm", "LArm_ForeArm1", 3.4f, 3.7f }, { "RArm_UpperArm", "RArm_ForeArm1", 3.4f, 3.7f },
					{ "LArm_ForeArm1", "LArm_Hand", 2.25f, 2.9f }, { "RArm_ForeArm1", "RArm_Hand", 2.25f, 2.9f },
				};
				for (UInt32 id : d.hosts) {
					NiNode* root = RootOf(id);
					Actor* who = ActorOf(id);
					if (!root || !who)
						continue;
					const float scale = G::World(root).scale;
					const bool male = actorUtils::IsActorMale(who);
					for (std::size_t k = 0; k < std::size(kParts); k++) {
						const RE::BSFixedString na(kParts[k].a), nb(kParts[k].b);
						NiAVObject* a = G::SkeletonNode(root, na);
						NiAVObject* b = G::SkeletonNode(root, nb);
						if (!a || !b)
							continue;
						char key[48];
						std::snprintf(key, sizeof(key), "%08X|body|%zu", id, k);
						AddCollider(d, id, key, G::World(a).pos, G::World(b).pos, (male ? kParts[k].man : kParts[k].woman) * P.bodyScale * scale, dt);
					}
				}
				for (UInt32 id : d.hosts) {   // a shaft, segment by segment
					Actor* a = ActorOf(id);
					std::vector<NiAVObject*> chain;
					if (!a || !AimChain(a, chain))
						continue;
					for (std::size_t k = 0; k + 1 < chain.size(); k++) {
						char key[48];
						std::snprintf(key, sizeof(key), "%08X|shaft|%zu", id, k);
						AddCollider(d, id, key, G::World(chain[k]).pos, G::World(chain[k + 1]).pos, P.shaftRadius, dt);
						d.capsules.back().shaft = true;
					}
				}
			}
			for (const Emitter& e : emitters)
				if (e.domain == d.id && e.hasNozzle)
					d.holeShapes.push_back(FluidSim::Hole{ ToLocal(d, e.lastNozzle), kNozzleHole / kUnitsPerMeter });
			for (const auto& [owner, kind] : d.holes) {
				V center, inward;
				if (OpeningNow(owner, kind, center, inward))
					d.holeShapes.push_back(FluidSim::Hole{ ToLocal(d, center), kHoleRadius / kUnitsPerMeter });
			}
			Canals(d, dt);
		}

		void Undress(Domain& d, float dt);
		void CanalStates(Domain& d);

		void Simulate(Domain& d, float dt)
		{
			if (d.particles.empty()) {
				d.accumulator = 0.0f;
				return;
			}
			d.accumulator += dt;
			Colliders(d, dt);
			FluidSim::Params sp;
			sp.cohesion = P.cohesion;
			sp.viscosity = P.viscosity;
			sp.stretch = P.stretch;
			sp.film = P.stickiness;
			sp.friction = P.friction;
			sp.slime = P.slime;
			sp.canalDrag = P.seep;
			sp.strings = P.strings;
			int steps = 0;
			while (d.accumulator >= kStep && steps < kMaxSteps) {
				d.solver.Step(d.particles, d.anchors, d.capsules, d.holeShapes, sp, kStep, d.canals);
				d.accumulator -= kStep;
				steps++;
			}
			if (steps == kMaxSteps)
				d.accumulator = 0.0f;
			std::erase_if(d.anchors, [](const FluidSim::Anchor& a) { return a.rest <= 0.0f; });
			Undress(d, dt);
			CanalStates(d);
		}

		// a woman who got dressed (our genitals shape gone: the body slot's armour replaced the naked body, as AAF does the
		// moment a scene ends) for kDressedGrace: what lies on her skin is under her clothes now and goes within a second;
		// it was drawn through them (in game, 10-17). A man is never "dressed" by this test, and his shaft is bare anyway
		void Undress(Domain& d, float dt)
		{
			static const RE::BSFixedString genitals("AnatomyGenitals");
			for (UInt32 id : d.hosts) {
				Actor* who = ActorOf(id);
				const bool covered = who && !actorUtils::IsActorMale(who) && G::Root(who) && !G::Find(G::Root(who), genitals.c_str());
				float& t = d.dressedFor[id];
				t = covered ? t + dt : 0.0f;
			}
			for (FluidSim::Particle& p : d.particles) {
				if (p.canal >= 0 && (std::size_t)p.canal < d.holes.size() && d.holes[p.canal].second != 2) {
					auto it = d.dressedFor.find(d.holes[p.canal].first);
					if (it != d.dressedFor.end() && it->second > kDressedGrace)
						p.life = (std::min)(p.life, 1.0f);
					continue;
				}
				if (!p.touching || p.surface < 0 || (std::size_t)p.surface >= d.capsuleOwners.size())
					continue;
				auto it = d.dressedFor.find(d.capsuleOwners[p.surface]);
				if (it != d.dressedFor.end() && it->second > kDressedGrace)
					p.life = (std::min)(p.life, 1.0f);
			}
		}

		// particles whose life ran out go; links across them break, anchors and blob seeds follow the new indices
		void Expire(Domain& d, float dt)
		{
			auto& ps = d.particles;
			bool expired = false;
			for (auto& p : ps) {
				p.life -= dt;
				expired = expired || p.life <= 0.0f;
			}
			if (!expired)
				return;
			std::vector<std::size_t> remap(ps.size(), kNone);
			std::size_t write = 0;
			bool previousKept = false;
			for (std::size_t read = 0; read < ps.size(); read++) {
				if (ps[read].life <= 0.0f) {
					if (previousKept)
						ps[write - 1].link = 0.0f;
					previousKept = false;
					continue;
				}
				remap[read] = write;
				ps[write++] = ps[read];
				previousKept = true;
			}
			ps.resize(write);
			for (Slot& s : blobs)
				if (s.domain == d.id && s.live) {
					s.seed = s.seed < remap.size() ? remap[s.seed] : kNone;
					s.live = s.seed != kNone;
				}
			std::erase_if(d.anchors, [&](FluidSim::Anchor& a) {
				a.particle = a.particle < remap.size() ? remap[a.particle] : kNone;
				return a.particle == kNone;
			});
		}

		// ---- drawing

		Slot* AcquireBlob(Domain& d)
		{
			for (int i = 0; i < kBlobPool; i++) {
				Slot& s = blobs[i];
				if (s.used || s.failed)
					continue;
				if (!s.root && !LoadBlob(i))
					continue;
				if (s.domain != d.id)
					s.live = false;
				s.used = true;
				s.domain = d.id;
				Attach(s, d.parent);
				return &s;
			}
			return nullptr;
		}

		Slot* AcquireRope(Domain& d)
		{
			for (int i = 0; i < kRopePool; i++) {
				Slot& s = ropes[i];
				if (s.used || s.failed)
					continue;
				if (!s.root && !LoadRope(i))
					continue;
				s.used = true;
				s.domain = d.id;
				Attach(s, d.parent);
				return &s;
			}
			return nullptr;
		}

		// a blob wrapped around its particles: each bone on the field's skin along its direction, turned so its vertex's
		// normal is the skin's
		void PoseBlob(Domain& d, Slot& s)
		{
			const NiTransform& pw = G::World(d.parent);
			V lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
			V mid(0, 0, 0);   // drawn P.blobScale larger about its middle (the physics keeps the true volume)
			for (const auto& sp : s.surface)
				mid += sp.pos;
			mid = mid * (1.0f / (float)(std::max<std::size_t>)(s.surface.size(), 1));
			for (std::size_t i = 0; i < s.bones.size(); i++) {
				NiTransform w;
				float m[3][3];
				ArcRotation(directions[i], s.surface[i].normal, m);
				StoreRotation(w, m);
				w.pos = ToWorld(d, mid + (s.surface[i].pos - mid) * P.blobScale);
				w.scale = 1.0f;
				Place(s.bones[i], w, pw);
				lo = V((std::min)(lo.x, w.pos.x), (std::min)(lo.y, w.pos.y), (std::min)(lo.z, w.pos.z));
				hi = V((std::max)(hi.x, w.pos.x), (std::max)(hi.y, w.pos.y), (std::max)(hi.z, w.pos.z));
			}
			Finish(s, pw, lo, hi);
		}

		// a drop: a sphere of a_radius, drawn out along its flight and flattened where it touches
		void PoseDrop(Domain& d, Slot& s, const V& a_center, float a_trueRadius, const V& a_velocity, const V* a_contact)
		{
			// drawn P.dropScale larger than its volume: one particle's true sphere is 0.26 units, flattened to 0.45 on a
			// body it vanished into the skin (in game, 10-17)
			const float a_radius = a_trueRadius * P.dropScale;
			const float flying = a_contact ? 0.0f : (std::max)(Length(a_velocity) - kStreakStart, 0.0f);
			const float stretch = 1.0f + (std::min)(flying * kStreak, kMaxStretch - 1.0f);
			const V alongV = Unit(a_velocity, V(0, 0, 1));
			const float squash = a_contact ? kSquash : 1.0f;
			const V n = a_contact ? *a_contact : V(0, 0, 1);
			// on a body: a flat disc (kSquash thick, 1 / sqrt(kSquash) wide, its volume kept) whose neighbours overlap into
			// one smear. At 0.45 a landed run read as beads on a string, wriggling worms (the owner, 10-17: "like non
			// newton liquid"); lowered toward the skin only so far that it still sits on top of it
			const V center = a_contact ? a_center - n * (a_radius * (1.0f - squash) * 0.4f) : a_center;
			const NiTransform& pw = G::World(d.parent);
			V lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
			for (std::size_t i = 0; i < s.bones.size(); i++) {
				const V dir = directions[i];
				const V off = Stretch(Stretch(dir, alongV, stretch), n, squash);
				const V facing = Unit(Stretch(Stretch(dir, alongV, 1.0f / stretch), n, 1.0f / squash), dir);
				NiTransform w;
				float m[3][3];
				ArcRotation(dir, facing, m);
				StoreRotation(w, m);
				w.pos = ToWorld(d, center + off * a_radius);
				w.scale = 1.0f;
				Place(s.bones[i], w, pw);
				lo = V((std::min)(lo.x, w.pos.x), (std::min)(lo.y, w.pos.y), (std::min)(lo.z, w.pos.z));
				hi = V((std::max)(hi.x, w.pos.x), (std::max)(hi.y, w.pos.y), (std::max)(hi.z, w.pos.z));
			}
			s.live = false;
			Finish(s, pw, lo, hi);
		}

		// a run as a rope: its bones spread along the run (past both ends by a cap), each as thick as the particles
		// there hold, thinner while they fade, rounded at a free end
		void PoseRope(Domain& d, Slot& s, std::size_t a_begin, std::size_t a_end, const FluidSim::Anchor* a_anchor)
		{
			const float fade = (std::max)(P.fade, 1e-3f);
			std::vector<FluidSim::Particle> pts(d.particles.begin() + a_begin, d.particles.begin() + a_end);
			if (a_anchor) {
				FluidSim::Particle tip = pts.back();
				tip.pos = a_anchor->point;
				pts.push_back(tip);
			}
			const std::size_t count = pts.size();
			std::vector<float> along(count, 0.0f);
			for (std::size_t k = 1; k < count; k++)
				along[k] = along[k - 1] + Length(pts[k].pos - pts[k - 1].pos);
			const float nominal = std::sqrt(kVolume / (kPi * kSlotSpacing * kSpacing));
			const float total = along.back();
			const float cap = nominal;
			auto gapAt = [&](std::size_t k) {
				const std::size_t lo = k > 0 ? k - 1 : 0;
				const std::size_t hi = (std::min)(k + 2, count - 1);
				return (along[hi] - along[lo]) / (float)(std::max<std::size_t>)(hi - lo, 1);
			};
			const int nb = (int)s.bones.size();
			std::vector<V> positions(nb);
			std::vector<float> radii(nb);
			std::size_t seg = 0;
			for (int b = 0; b < nb; b++) {
				const float t = (float)b / (nb - 1);
				const float sv = -cap + (total + 2.0f * cap) * t;
				const float clamped = std::clamp(sv, 0.0f, total);
				while (seg + 2 < count && along[seg + 1] < clamped)
					seg++;
				const auto& a = pts[seg];
				const auto& c = pts[(std::min)(seg + 1, count - 1)];
				const float len = along[(std::min)(seg + 1, count - 1)] - along[seg];
				const float u = len > 1e-6f ? std::clamp((clamped - along[seg]) / len, 0.0f, 1.0f) : 0.0f;
				V point = a.pos + (c.pos - a.pos) * u;
				const V tangent = Unit(c.pos - a.pos, V(0, 0, 1));
				if (sv < 0.0f)
					point = point + tangent * sv;
				else if (sv > total)
					point = point + tangent * (sv - total);
				const float gap = (std::max)(gapAt(seg), 0.3f * kSpacing);
				float radius = std::clamp(std::sqrt(kVolume / (kPi * gap)), 0.2f * nominal, 1.6f * nominal);
				radius *= std::sqrt(std::clamp((a.life + (c.life - a.life) * u) / fade, 0.0f, 1.0f));
				if (b == 0 || (b + 1 == nb && !a_anchor))
					radius *= 0.55f;
				else if (b == 1 || (b + 2 == nb && !a_anchor))
					radius *= 0.85f;
				positions[b] = ToWorld(d, point);
				radii[b] = radius * kUnitsPerMeter;
			}
			const NiTransform& pw = G::World(d.parent);
			V lo = positions[0], hi = positions[0];
			for (int b = 0; b < nb; b++) {
				const V dir = b + 1 < nb ? positions[b + 1] - positions[b] : positions[b] - positions[b - 1];
				Place(s.bones[b], Frame(positions[b], dir, (std::max)(radii[b], 0.0005f)), pw);
				lo = V((std::min)(lo.x, positions[b].x), (std::min)(lo.y, positions[b].y), (std::min)(lo.z, positions[b].z));
				hi = V((std::max)(hi.x, positions[b].x), (std::max)(hi.y, positions[b].y), (std::max)(hi.z, positions[b].z));
			}
			Finish(s, pw, lo, hi);
		}

		// OStrands' Draw: crowded particles ("bulk") cluster; a small cluster is a drop, a big one splits into pieces each
		// wrapped by a blob (one that drew there last frame keeps its piece and eases its skin over); runs are ropes; a lone
		// particle not inside a blob is a drop
		void Draw(Domain& d, float dt)
		{
			if (directions.empty() && !LoadBlob(0))
				return;
			auto& ps = d.particles;
			std::vector<FluidSim::Particle> shown = ps;   // inside her (a canal): weightless for the surfaces
			for (FluidSim::Particle& q : shown)
				if (q.canal >= 0)
					q.life = 0.0f;
			const FluidSim::Density field(shown, kKernel, P.fade);
			std::vector<std::size_t> pooled;
			for (std::size_t i = 0; i < ps.size(); i++) {
				if (ps[i].canal >= 0) {
					ps[i].bulk = false;
					continue;
				}
				if (!ps[i].asleep) {
					const float c = field.Count(ps[i].pos);
					ps[i].bulk = ps[i].bulk ? c >= kBulkLeave : c >= kBulkEnter;
				}
				if (ps[i].bulk)
					pooled.push_back(i);
			}
			const auto clusters = FluidSim::FindClusters(ps, pooled, 2.0f * kSpacing);
			std::vector<std::size_t> owner(ps.size(), kNone);
			for (std::size_t c = 0; c < clusters.size(); c++)
				for (std::size_t m : clusters[c].members)
					owner[m] = c;
			std::vector<std::vector<int>> hints(clusters.size());
			for (int b = 0; b < kBlobPool; b++) {
				Slot& s = blobs[b];
				if (s.domain != d.id)
					continue;
				if (s.live && s.seed < ps.size() && owner[s.seed] != kNone && !s.used) {
					hints[owner[s.seed]].push_back(b);
					s.used = true;
				} else
					s.live = false;
			}
			const float blend = 1.0f - std::exp(-dt / kSmoothing);
			std::vector<int> claimed;
			struct Droplet
			{
				V center;
				float radius;
				V velocity;
				bool touching;
				V contact;
			};
			std::vector<Droplet> droplets;
			auto contactOf = [&](const std::vector<std::size_t>& a_members, const V& a_center, V& a_n) {
				for (std::size_t m : a_members) {
					const auto& p = ps[m];
					if (!p.touching || p.surface < 0 || (std::size_t)p.surface >= d.capsules.size())
						continue;
					const auto& cap = d.capsules[p.surface];
					a_n = Unit(p.pos - (cap.a + (cap.b - cap.a) * p.along), V(0, 0, 1));
					return true;
				}
				if (a_center.z < kSpacing) {   // on the floor
					a_n = V(0, 0, 1);
					return true;
				}
				return false;
			};
			auto velocityOf = [&](const std::vector<std::size_t>& a_members) {
				V sum(0, 0, 0);
				for (std::size_t m : a_members)
					sum += ps[m].vel;
				return a_members.empty() ? sum : sum * (1.0f / (float)a_members.size());
			};
			auto weightOf = [&](const std::vector<std::size_t>& a_members) {
				float sum = 0.0f;
				for (std::size_t m : a_members)
					sum += field.Weight(ps[m]);
				return a_members.empty() ? 0.0f : sum / (float)a_members.size();
			};
			std::vector<SurfacePoint> scratch;
			for (std::size_t c = 0; c < clusters.size(); c++) {
				const auto& cl = clusters[c];
				if (cl.members.size() < kBlobMinimum) {
					Droplet dr{ cl.center, FluidSim::VolumeRadius(cl.members.size(), kVolume) * std::cbrt(weightOf(cl.members)), velocityOf(cl.members) };
					dr.touching = contactOf(cl.members, cl.center, dr.contact);
					droplets.push_back(dr);
					continue;
				}
				std::vector<std::size_t> seeds;
				for (int b : hints[c])
					seeds.push_back(blobs[b].seed);
				const bool settled = std::all_of(cl.members.begin(), cl.members.end(),
					[&](std::size_t m) { return ps[m].asleep && ps[m].life > P.fade; });
				for (const auto& piece : FluidSim::SplitCluster(ps, cl, kPieceRadius, seeds)) {
					Slot* s = piece.hint != kNone ? &blobs[hints[c][piece.hint]] : AcquireBlob(d);
					if (!s)
						continue;
					if (piece.hint != kNone)
						claimed.push_back(hints[c][piece.hint]);
					const bool warm = s->live && s->surface.size() == directions.size();
					if (settled && warm && piece.hint != kNone) {   // asleep: nothing to redo, it stays as drawn
						s->seed = piece.seed;
						continue;
					}
					if (!FluidSim::Wrap(field, ps[piece.seed].pos, piece.extent + kKernel, kThreshold, directions, scratch)) {
						PoseDrop(d, *s, piece.center, FluidSim::VolumeRadius(piece.count, kVolume), V(0, 0, 0), nullptr);
						continue;
					}
					if (warm)
						for (std::size_t i = 0; i < scratch.size(); i++) {
							auto& cur = s->surface[i];
							const auto& tgt = scratch[i];
							cur.pos = cur.pos + (tgt.pos - cur.pos) * blend;
							cur.normal = Unit(cur.normal + (tgt.normal - cur.normal) * blend, tgt.normal);
							cur.dist = tgt.dist;
						}
					else
						s->surface = scratch;
					s->seed = piece.seed;
					s->live = true;
					PoseBlob(d, *s);
				}
			}
			for (auto& hinted : hints)
				for (int b : hinted)
					if (std::find(claimed.begin(), claimed.end(), b) == claimed.end())
						blobs[b].used = false, blobs[b].live = false;

			for (const auto& run : FluidSim::FindRuns(ps)) {
				if (ps[run.begin].canal >= 0 && ps[run.end - 1].canal >= 0)
					continue;   // inside her
				auto held = std::find_if(d.anchors.begin(), d.anchors.end(), [&](const FluidSim::Anchor& a) { return a.particle + 1 == run.end; });
				const FluidSim::Anchor* anchor = held != d.anchors.end() ? &*held : nullptr;
				if (run.Size() >= 2 || anchor) {
					if (Slot* s = AcquireRope(d))
						PoseRope(d, *s, run.begin, run.end, anchor);
					continue;
				}
				const auto& single = ps[run.begin];
				if (field.BulkCount(single.pos) > kCovered)
					continue;
				Droplet dr{ single.pos, FluidSim::VolumeRadius(1, kVolume) * std::cbrt(field.Weight(single)), single.vel };
				dr.touching = contactOf({ run.begin }, single.pos, dr.contact);
				droplets.push_back(dr);
			}
			for (const auto& a : d.anchors)
				if (a.particle < ps.size() && ps[a.particle].bulk)
					if (Slot* s = AcquireRope(d))
						PoseRope(d, *s, a.particle, a.particle + 1, &a);
			for (const auto& dr : droplets) {
				if (dr.radius <= 0.0f)
					continue;
				if (Slot* s = AcquireBlob(d))
					PoseDrop(d, *s, dr.center, dr.radius, dr.velocity, dr.touching ? &dr.contact : nullptr);
			}
		}

		void TickImpl()
		{
			std::lock_guard<std::mutex> guard(lock);
			LARGE_INTEGER now, freq;
			QueryPerformanceCounter(&now);
			QueryPerformanceFrequency(&freq);
			float dt = lastTick.QuadPart ? (float)(now.QuadPart - lastTick.QuadPart) / (float)freq.QuadPart : 0.0f;
			lastTick = now;
			clock += (std::min)((std::max)(dt, 0.0f), 0.05f);
			dt = (std::min)((std::max)(dt, 0.0f), 0.05f);
			if (dt <= 0.0f)
				return;
			if (!P.enabled) {
				for (auto& d : domains)
					Release(*d);
				domains.clear();
				emitters.clear();
				reservoirs.clear();
				return;
			}
			RefreshOpenings();
			std::vector<Queued> queued;
			{
				std::lock_guard<std::mutex> l(queueLock);
				queued.swap(queue);
			}
			for (const Queued& q : queued) {
				if (std::any_of(emitters.begin(), emitters.end(), [&](const Emitter& e) { return e.him == q.him; }))
					continue;
				Emitter e;
				e.id = ++nextEmitter;
				e.him = q.him;
				e.other = q.other;
				e.her = q.her;
				e.kind = q.her ? q.kind : -1;
				emitters.push_back(e);
				Note("[fluid] climax of {:08X}: {}", q.him, q.her ? std::format("inside {:08X}'s opening {}", q.her, q.kind) : std::string("outside"));
			}
			for (std::size_t i = 0; i < emitters.size();)
				if (!StepEmitter(emitters[i], dt))
					emitters.erase(emitters.begin() + i);
				else
					i++;
			for (std::size_t i = 0; i < reservoirs.size();)
				if (!Leak(reservoirs[i], dt))
					reservoirs.erase(reservoirs.begin() + i);
				else
					i++;

			SendMouths();
			for (Slot& s : blobs)
				s.used = false;
			for (Slot& s : ropes)
				s.used = false;
			for (std::size_t i = 0; i < domains.size();) {
				Domain& d = *domains[i];
				NiNode* parent = RootOf(d.parentId);
				const bool referenced = std::any_of(emitters.begin(), emitters.end(), [&](const Emitter& e) { return e.domain == d.id; }) ||
				                        std::any_of(reservoirs.begin(), reservoirs.end(), [&](const Reservoir& r) { return r.domain == d.id; });
				d.idle = d.particles.empty() && !referenced ? d.idle + dt : 0.0f;
				if (!parent || parent != d.parent || d.idle > 2.0f) {   // its 3D replaced (a load, a cell change) or done
					Release(d);
					domains.erase(domains.begin() + i);
					continue;
				}
				Simulate(d, dt);
				Expire(d, dt);
				Draw(d, dt);
				d.stat += dt;
				if (d.stat >= 5.0f && d.particles.size() != d.lastCount) {   // stats: their own lines, not Note's budget
					d.stat = 0.0f;
					d.lastCount = d.particles.size();
					int b = 0, rp = 0, touching = 0, asleep = 0;
					for (const Slot& s : blobs)
						b += s.used && s.domain == d.id;
					for (const Slot& s : ropes)
						rp += s.used && s.domain == d.id;
					for (const auto& q : d.particles)
						touching += q.touching, asleep += q.asleep;
					spdlog::info("[fluid] pool {}: {} particles ({} on a body, {} asleep), {} blobs, {} ropes, {} colliders", d.id,
						d.particles.size(), touching, asleep, b, rp, d.capsules.size());
				}
				i++;
			}
			for (Slot& s : blobs)
				if (!s.used && s.root)
					s.live = false, Hide(s);
			for (Slot& s : ropes)
				if (!s.used && s.root)
					Hide(s);
		}
	}

	void LoadConfig(INIReader& reader)
	{
		std::lock_guard<std::mutex> guard(lock);
		P = Params();
		auto f = [&](const char* k, float d) { return (float)reader.GetReal("Fluid", k, d); };
		auto i = [&](const char* k, int d) { return (int)reader.GetInteger("Fluid", k, d); };
		P.enabled = reader.GetBoolean("Fluid", "enabled", true);
		P.amount = std::clamp(f("amount", P.amount), 0.0f, 10.0f);
		P.strength = std::clamp(f("strength", P.strength), 0.0f, 5.0f);
		P.pulses = std::clamp(i("pulses", P.pulses), 1, 20);
		P.cohesion = std::clamp(f("cohesion", P.cohesion), 0.0f, 20.0f);
		P.viscosity = std::clamp(f("viscosity", P.viscosity), 0.0f, 100.0f);
		P.stretch = std::clamp(f("stretch", P.stretch), 0.0f, 30.0f);
		P.stickiness = std::clamp(f("stickiness", P.stickiness), 0.0f, 100.0f);
		P.friction = std::clamp(f("friction", P.friction), 0.0f, 200.0f);
		P.slime = std::clamp(f("slime", P.slime), 0.1f, 30.0f);
		P.strings = std::clamp((int)f("strings", (float)P.strings), 0, 64);
		P.overflow = std::clamp(f("overflow", P.overflow), 0.0f, 1.0f);
		P.lifetime = std::clamp(f("lifetime", P.lifetime), 1.0f, 600.0f);
		P.fade = std::clamp(f("fade", P.fade), 0.1f, P.lifetime);
		P.maxParticles = std::clamp(i("maxParticles", P.maxParticles), 64, 4096);
		P.capacity[0] = (std::max)(0.0f, f("capacityVagina", P.capacity[0]));
		P.capacity[1] = (std::max)(0.0f, f("capacityAnus", P.capacity[1]));
		P.capacity[2] = (std::max)(0.0f, f("capacityMouth", P.capacity[2]));
		P.reservoirLife = std::clamp(f("reservoirLife", P.reservoirLife), 1.0f, 900.0f);
		P.moveGain = std::clamp(f("moveGain", P.moveGain), 0.0f, 20.0f);
		P.swallow = std::clamp(f("swallow", P.swallow), 0.0f, 1.0f);
		P.seep = std::clamp(f("seep", P.seep), 50.0f, 50000.0f);
		P.collide = reader.GetBoolean("Fluid", "collide", true);
		P.shaftRadius = std::clamp(f("shaftRadius", P.shaftRadius), 0.0f, 10.0f);
		P.bodyScale = std::clamp(f("bodyScale", P.bodyScale), 0.0f, 3.0f);
		P.dropScale = std::clamp(f("dropScale", P.dropScale), 0.1f, 10.0f);
		P.blobScale = std::clamp(f("blobScale", P.blobScale), 0.1f, 5.0f);
	}

	void Tick()
	{
		__try {
			TickImpl();
		} __except (1) {
			static bool said = false;
			if (!said) {
				said = true;
				spdlog::info("[fluid] an exception in the fluid update: fluid off for this session");
			}
			P.enabled = false;
		}
	}

	bool Ready()
	{
		std::lock_guard<std::mutex> guard(lock);
		return P.enabled && LoadBlob(0) && LoadRope(0);
	}

	int MouthState(unsigned int her)
	{
		std::lock_guard<std::mutex> l(mouthLock);
		auto it = mouths.find(her);
		return it != mouths.end() && it->second.second > clock ? it->second.first : 0;
	}

	void Climax(unsigned int him, unsigned int other, unsigned int her, int kind)
	{
		std::lock_guard<std::mutex> l(queueLock);
		if (queue.size() < 16)
			queue.push_back(Queued{ him, other, her, kind });
	}

	void Reset()
	{
		{
			std::lock_guard<std::mutex> l(queueLock);
			queue.clear();
		}
		std::lock_guard<std::mutex> guard(lock);
		for (auto& d : domains)
			Release(*d);
		domains.clear();
		emitters.clear();
		reservoirs.clear();
		openings.clear();
		for (Slot& s : blobs)
			s.live = false, s.domain = 0, s.used = false, Detach(s);
		for (Slot& s : ropes)
			s.domain = 0, s.used = false, Detach(s);
		lastTick.QuadPart = 0;
	}
}
