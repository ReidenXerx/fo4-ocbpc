// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16 (see Strands.h; the design is OStrands').
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Strands.h"
#include "Fluid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ActorEntry.h"
#include "Aim.h"
#include "CollisionHub.h"
#include "Hook.h"
#include "NiMath.h"

#include <windows.h>

namespace Strands
{
	namespace
	{
		// a strand's points: as many as its mesh has bones (Strand00, Strand01, ... read at load), at most kMaxBones; Anatomy
		// 1.2.6 shipped 10, 1.2.7 ships 16 (smoother bends). Either engine runs either mesh
		constexpr int kMaxBones = 32;
		constexpr int kMinBones = 4;
		constexpr int kPool = 16;  // meshes\Anatomy\Strand\Strand00..15.nif: one Demand prototype each (no clone bound);
		                           // a missing copy only leaves its slot off (Anatomy 1.2.6-1.2.7 ship 8)
		constexpr float kPi = 3.14159265f;
		// OStrands' StrandManager.cpp constants
		constexpr int kSubsteps = 2;
		constexpr int kIterations = 6;
		constexpr float kMaxBudget = 3.0f;
		constexpr float kFadeIn = 0.12f;
		constexpr float kSwingRelax = 0.1f;
		constexpr float kMinSwing = 0.6f;
		constexpr float kStrokeNear = 0.1f, kStrokeNearMax = 0.6f, kStrokeGap = 0.1f, kStrokeGapMax = 0.6f;
		constexpr float kSkinMargin = 0.15f;    // OStrands: kept this far off a body
		constexpr float kSkinFriction = 0.3f;   // OStrands: of the velocity along a body, lost on touching it

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

		// OStrands' [Shape], [Spawn] and [Physics] defaults where they apply (its OStrands.ini)
		struct Params
		{
			bool enabled = true;
			bool bridges = true;        // strands on every outstroke, between her rim and his shaft
			float bridgeRate = 2.1f;    // per second, the budget a pair earns
			int maxPerStroke = 2;
			int maxPerPair = 5;
			float cooldown = 0.25f;     // seconds between two strokes' strands
			float pulloutChance = 0.6f; // a pull-out leaves a strand between her entrance and his tip this often
			float maxStart = 20.0f;     // units from her entrance to his tip when Aim lets go: farther, no pull-out strand
			float shaftRadius = 1.0f;   // where a bridge meets his shaft, off its axis
			float rimRadius = 1.6f;     // where it meets her lips, off the opening's centre (0.9 left them ~0.3 long, 10-16)
			float radius = 0.10f;       // at its first length; its volume is kept, so it thins as it stretches
			float minRadius = 0.035f;   // thinner than this, it snaps
			float maxLength = 14.0f;    // stretched this far past its first length, it snaps
			float gravity = 350.0f;     // units per second squared
			float damping = 0.9f;       // velocity kept per 1/120 s
			float viscosity = 4.0f;     // how fast a stretched segment's rest length follows it
			float life = 4.0f;          // seconds: still whole by then, it snaps
			float fade = 0.4f;          // seconds to thin away once snapped
			bool collide = true;        // pushed out of both bodies' collision spheres (OCBPCollisionConfig's)
			float clearance = 2.0f;     // points this near an end are not pushed: the ends sit on (in) the bodies
			// climax (AAF's CLIMAX-tagged stage, Anatomy:Arousal -> AnatomyAim.Climax): outside her, shots from his tip;
			// inside her, drips from her opening once he is out (the owner unparked fluids, 2026-10-16)
			bool climax = true;
			int shotsMin = 3, shotsMax = 5;   // pulses per climax
			float shotInterval = 0.7f;  // seconds between pulses
			float shotSpeed = 180.0f;   // units per second at the tip, the first pulse; each next one 0.8 of the last
			float shotSpread = 0.12f;   // off his shaft's axis, as a fraction of the speed
			float shotGravity = 600.0f; // in flight (a stream is not yet a viscous strand)
			float shotRadius = 0.16f;
			float shotRelease = 0.25f;  // seconds the stream stays on his tip
			float shotLife = 7.0f;      // seconds a landed stream lies there before it thins away
			int drips = 5;              // per climax inside her
			float dripInterval = 1.2f;  // seconds between drips
			float dripRadius = 0.12f;
			float dripStretch = 1.2f;   // seconds for a drip to grow by e
			float dripMax = 6.0f;       // units: longer, it lets go of her
			float dripLife = 4.0f;
			float climaxCooldown = 20.0f;   // seconds: AAF repeats a climax stage's events; one climax per man in this time
		} P;

		// one end: a point fixed in a node's frame (OStrands' End), so it rides her pelvis or his shaft
		struct End
		{
			RE::NiPointer<NiAVObject> node;
			NiPoint3 local;

			NiPoint3 World() const
			{
				const NiTransform& w = G::World(node.get());
				return w.pos + w.rot.Transpose() * (local * w.scale);
			}

			static End At(NiAVObject* a_node, const NiPoint3& a_world)
			{
				const NiTransform& w = G::World(a_node);
				return End{ RE::NiPointer<NiAVObject>(a_node), (w.rot * (a_world - w.pos)) * (1.0f / w.scale) };
			}
		};

		struct Strand
		{
			// the mesh: its own Demand prototype, loaded at its first use
			RE::NiPointer<NiNode> root;
			std::array<NiAVObject*, kMaxBones> bones{};
			int n = 0;                   // its points: the mesh's bone count
			NiNode* attachedTo = nullptr;
			bool failed = false;
			// the strand
			bool active = false;
			int kind = 0;                // 0 a bridge or pull-out (both ends fixed), 1 a shot, 2 a drip (a free end)
			bool topPinned = true, bottomPinned = true;
			bool landed = false;         // a shot's free end stuck to a body
			float releaseAt = 0.0f;      // a shot: its age when it leaves his tip
			float fixedRadius = 0.0f;    // shots and drips: their radius (bridges: by their volume)
			UInt32 owner = 0;            // whose 3D carries it: her for a bridge or drip, him for a shot
			UInt32 partner = 0;          // the other one (0: none)
			std::uint64_t pair = 0;      // owner << 2 | opening kind
			End top, bottom;
			std::array<NiPoint3, kMaxBones> pos{}, prev{};
			std::array<float, kMaxBones - 1> rest{};
			float volume = 0.0f;
			float startLength = 1.0f;
			float radius = 0.0f;
			float age = 0.0f;
			float fade = 1.0f;
			float fadeRate = 0.0f;
			float previousStep = 0.0f;
			int breakAt = -1;
		};
		std::array<Strand, kPool> pool;

		// OStrands' Swing: a value's recent low and high; Arrived near the low, Left past a gap above it
		enum class Crossing { kNone, kArrived, kLeft };
		struct Swing
		{
			float low = 0.0f, high = 0.0f;
			bool primed = false, engaged = false;

			Crossing Step(float a_value, float a_delta)
			{
				if (!primed) {
					low = high = a_value;
					primed = true;
					return Crossing::kNone;
				}
				const float relax = (high - low) * kSwingRelax * a_delta;
				low = (std::min)(low + relax, a_value);
				high = (std::max)(high - relax, a_value);
				const float range = high - low;
				if (range < kMinSwing)
					return Crossing::kNone;
				const float nearAt = low + (std::min)(range * kStrokeNear, kStrokeNearMax);
				const float leaveAt = nearAt + (std::min)(range * kStrokeGap, kStrokeGapMax);
				if (!engaged && a_value < nearAt) {
					engaged = true;
					return Crossing::kArrived;
				}
				if (engaged && a_value > leaveAt) {
					engaged = false;
					return Crossing::kLeft;
				}
				return Crossing::kNone;
			}
		};

		// one opening and the shaft in it (key: owner << 2 | kind)
		struct PairState
		{
			UInt32 partner = 0;          // whose shaft held it last frame (0: none)
			UInt32 lastPartner = 0;      // whose shaft held it last, and how long ago: Aim's lock flickers off between
			float since = 1e9f;          // strokes, so a climax "inside" is one within a moment of a hold (10-16)
			Swing stroke;
			bool bridgePending = false;
			float budget = 0.0f;
			float cooldown = 0.0f;
			std::uint32_t seen = 0;
		};
		std::unordered_map<std::uint64_t, PairState> pairs;
		std::uint32_t frame = 0;

		std::uint32_t rng = 0x2545F491u;
		LARGE_INTEGER lastTick{};
		int logged = 0;   // the first starts of a session, in the log
		std::mutex lock;  // the scan thread's Tick against a load's Reset (Canal.cpp, Skirt.cpp: the same)

		float Random01()
		{
			rng = rng * 1664525u + 1013904223u;
			return (float)((rng >> 8) & 0xFFFF) / 65535.0f;
		}

		float Dot(const NiPoint3& a, const NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

		NiPoint3 Cross(const NiPoint3& a, const NiPoint3& b)
		{
			return NiPoint3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
		}

		float Length(const NiPoint3& v) { return std::sqrt(Dot(v, v)); }

		NiPoint3 Unit(const NiPoint3& v, const NiPoint3& a_fallback = NiPoint3(1, 0, 0))
		{
			const float l = Length(v);
			return l > 1e-6f ? v * (1.0f / l) : a_fallback;
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

		// the node her opening is measured in (Aim.cpp AddAnatomyTargets: Pelvis_skin; a mouth: the head)
		NiAVObject* OpeningNode(Actor* a_her, int a_kind)
		{
			NiAVObject* root = G::Root(a_her);
			if (!root)
				return nullptr;
			static RE::BSFixedString pelvis("Pelvis_skin"), head("Head");
			return G::SkeletonNode(root, a_kind == 2 ? head : pelvis);
		}

		bool Load(Strand& s, int a_index)
		{
			const auto fn = Hook::Resolve(kDemand);
			if (!fn) {
				G::Once("strands|nodemand", "[strands] BSModelDB::Demand not resolved on this executable; strands off");
				return false;
			}
			char path[64];
			std::snprintf(path, sizeof(path), "Anatomy\\Strand\\Strand%02d.nif", a_index);   // "meshes\" is put in front
			RE::NiPointer<NiNode> model;
			const ModelArgs args{};
			const auto error = reinterpret_cast<Demand_t>(*fn)(path, &model, args);
			if (error != 0 || !model) {
				G::Once(std::string("strands|nomesh|") + path, "[strands] could not load meshes\\{} (error {}): that strand stays off",
					path, error);
				return false;
			}
			s.n = 0;
			for (int i = 0; i < kMaxBones; i++) {
				char name[16];
				std::snprintf(name, sizeof(name), "Strand%02d", i);
				if (!(s.bones[i] = G::Find(model.get(), name)))
					break;
				s.n = i + 1;
			}
			if (s.n < kMinBones) {
				spdlog::info("[strands] meshes\\{} has {} Strand bones (at least {}): that strand stays off", path, s.n, kMinBones);
				return false;
			}
			G::Once("strands|bones|" + std::to_string(s.n), "[strands] a strand mesh with {} points", s.n);
			// the identity, spelt out: NiTransform() zero-fills its matrix (NiMatrix43() : arr{}), and a zero rotation
			// let the game's own update collapse every child onto the root each frame (2026-10-16, runs 1-6)
			NiTransform identity;
			identity.rot.data[0][0] = identity.rot.data[1][1] = identity.rot.data[2][2] = 1.0f;
			G::Local(model.get()) = identity;
			s.root = model;
			return true;
		}

		void Detach(Strand& s)
		{
			if (s.root && s.root->parent)
				s.root->parent->DetachChild(s.root.get());
			s.attachedTo = nullptr;
		}

		// under her 3D. A shape draws only when AttachChild itself puts it into the live tree under a root the game
		// processed (an actor's 3D): one that arrives inside our root, attached later, never draws, nor does any under
		// the cell's node or a fade node of ours (2026-10-16, tests 13-20). So every shape is detached and attached again
		void Attach(Strand& s, NiNode* a_parent)
		{
			if (s.attachedTo == a_parent && s.root->parent == a_parent)
				return;
			Detach(s);
			a_parent->AttachChild(s.root.get(), true);
			for (UInt16 i = 0; i < G::ChildCount(s.root.get()); i++) {
				NiAVObject* c = G::Child(s.root.get(), i);
				if (c && !G::AsNode(c)) {
					RE::NiPointer<NiAVObject> keep(c);   // Detach releases a reference
					s.root->DetachChild(c);
					s.root->AttachChild(c, true);       // the hole Detach left: the same slot
				}
			}
			s.attachedTo = a_parent;
		}

		void Retire(Strand& s)
		{
			s.active = false;
			s.top = End{};
			s.bottom = End{};
			Detach(s);
		}

		int Count(std::uint64_t a_pair)
		{
			int n = 0;
			for (const Strand& s : pool)
				n += s.active && s.pair == a_pair;
			return n;
		}

		Strand* Acquire()
		{
			for (int i = 0; i < kPool; i++) {
				Strand& s = pool[i];
				if (s.active || s.failed)
					continue;
				if (!s.root && !Load(s, i)) {
					s.failed = true;
					continue;
				}
				return &s;
			}
			return nullptr;
		}

		// OStrands' StartBetween: a rope from top to bottom at rest, its volume set by its first length
		void Start(Strand& s, UInt32 a_owner, UInt32 a_partner, std::uint64_t a_pair, End a_top, End a_bottom, NiNode* a_parent)
		{
			const NiPoint3 a = a_top.World(), b = a_bottom.World();
			const float length = Length(b - a);
			s.active = true;
			s.kind = 0;
			s.topPinned = s.bottomPinned = true;
			s.landed = false;
			s.fixedRadius = 0.0f;
			s.owner = a_owner;
			s.partner = a_partner;
			s.pair = a_pair;
			s.top = std::move(a_top);
			s.bottom = std::move(a_bottom);
			for (int i = 0; i < s.n; i++)
				s.pos[i] = s.prev[i] = a + (b - a) * ((float)i / (s.n - 1));
			s.rest.fill((std::max)(length / (s.n - 1), 0.02f));
			s.volume = kPi * P.radius * P.radius * (std::max)(length, 1.0f);
			s.startLength = length;
			s.radius = P.radius;
			s.age = 0.0f;
			s.fade = 1.0f;
			s.fadeRate = 0.0f;
			s.previousStep = 0.0f;
			s.breakAt = -1;
			Attach(s, a_parent);
		}

		// a strand with one free end (OStrands' drip kind): every point at the top, moving with a_velocity along it so it
		// streams out (a shot), or barely (a drip)
		void StartFree(Strand& s, int a_kind, UInt32 a_owner, UInt32 a_partner, End a_top, const NiPoint3& a_velocity, float a_radius,
			NiNode* a_parent)
		{
			const NiPoint3 a = a_top.World();
			s.active = true;
			s.kind = a_kind;
			s.topPinned = true;
			s.bottomPinned = false;
			s.landed = false;
			s.releaseAt = P.shotRelease;
			s.fixedRadius = a_radius;
			s.owner = a_owner;
			s.partner = a_partner;
			s.pair = 0;
			s.top = std::move(a_top);
			s.bottom = End{};
			constexpr float kFrame = 1.0f / 60.0f;
			for (int i = 0; i < s.n; i++) {
				const float t = (float)i / (s.n - 1);
				s.pos[i] = a + a_velocity * (t * kFrame);
				s.prev[i] = a;
			}
			s.rest.fill(0.02f);
			s.volume = 0.0f;
			s.startLength = 0.0f;
			s.radius = a_radius;
			s.age = 0.0f;
			s.fade = 1.0f;
			s.fadeRate = 0.0f;
			s.previousStep = 0.0f;
			s.breakAt = -1;
			Attach(s, a_parent);
		}

		void Snap(Strand& s, float a_fadeTime)
		{
			if (s.breakAt >= 0)
				return;
			const int mid = s.n / 2 - 2 + (int)(Random01() * 3.0f);   // OStrands: n / 2 - 2 .. n / 2
			s.breakAt = (std::clamp)(mid, 0, s.n - 2);
			s.fadeRate = 1.0f / (std::max)(a_fadeTime, 0.05f);
		}

		// both bodies' collision spheres this frame (CollisionHub's, positioned earlier in this scan), with their node
		struct Ball
		{
			const Sphere* sphere;
			NiAVObject* node;
		};
		void Spheres(const Actor* a_her, const Actor* a_him, std::vector<Ball>& out)
		{
			out.clear();
			if (!P.collide)
				return;
			for (const Collision& c : otherColliders)
				if (!c.isProp && c.colliderActor && (c.colliderActor == a_her || c.colliderActor == a_him))
					for (const Sphere& sp : c.collisionSpheres)
						out.push_back(Ball{ &sp, c.CollisionObject });
		}

		// OStrands' Simulate: substeps of verlet under gravity, the ends pinned, a rope's constraints (none across a
		// break), the points pushed out of the bodies (OStrands' capsules: here the engine's spheres; not within
		// clearance of an end, which sits on or in a body), and viscosity: a stretched segment's rest flows after it
		void Simulate(Strand& s, float a_delta, const std::vector<Ball>& a_spheres)
		{
			if (s.kind == 1 && s.topPinned && s.age >= s.releaseAt)
				s.topPinned = false;   // the pulse is over: the stream leaves his tip
			const NiPoint3 a = s.topPinned ? s.top.World() : s.pos[0];
			const NiPoint3 b = s.bottomPinned ? s.bottom.World() : s.pos[s.n - 1];
			const bool flying = s.kind == 1 && !s.landed;
			const NiPoint3 gravity(0, 0, -(flying ? P.shotGravity : P.gravity));
			const float h = a_delta / kSubsteps;
			const float damping = h > 0.0f ? std::pow((std::clamp)(P.damping, 0.0f, 1.0f), h * 120.0f) : 1.0f;
			const int last = s.n - 1;
			const int first = s.topPinned ? 1 : 0, end = s.bottomPinned ? last : last + 1;   // the free points: [first, end)
			for (int step = 0; step < kSubsteps; step++) {
				const float scale = (flying ? 1.0f : damping) * (s.previousStep > 0.0f ? h / s.previousStep : 1.0f);
				for (int i = first; i < end; i++) {
					const NiPoint3 v = (s.pos[i] - s.prev[i]) * scale;
					s.prev[i] = s.pos[i];
					s.pos[i] = s.pos[i] + v + gravity * (h * h);
				}
				s.previousStep = h;
				if (s.topPinned)
					s.pos[0] = a;
				if (s.bottomPinned)
					s.pos[last] = b;
				if (flying) {   // in flight the stream lengthens behind its free end: no segment holds it back
					const float span = Length(s.pos[last] - s.pos[0]) / last;
					for (int k = 0; k < last; k++)
						s.rest[k] = (std::max)(s.rest[k], span);
				} else if (s.kind == 2 && s.topPinned) {   // a drip grows under its own weight (OStrands' dripStretchTime)
					const float growth = std::exp(h / (std::max)(P.dripStretch, 0.05f));
					for (int k = 0; k < last; k++)
						s.rest[k] *= growth;
				}
				for (int it = 0; it < kIterations; it++) {
					for (int k = 0; k < last; k++) {
						if (k == s.breakAt)
							continue;
						const NiPoint3 d = s.pos[k + 1] - s.pos[k];
						const float l = Length(d);
						if (l < 1e-5f)
							continue;
						if (s.kind != 0 && l <= s.rest[k])
							continue;   // a free strand is a rope: it pulls, never pushes
						const NiPoint3 fix = d * ((l - s.rest[k]) / l);
						const bool pinnedA = k == 0 && s.topPinned, pinnedB = k + 1 == last && s.bottomPinned;
						if (pinnedA && pinnedB)
							continue;
						if (pinnedA)
							s.pos[k + 1] = s.pos[k + 1] - fix;
						else if (pinnedB)
							s.pos[k] = s.pos[k] + fix;
						else {
							s.pos[k] = s.pos[k] + fix * 0.5f;
							s.pos[k + 1] = s.pos[k + 1] - fix * 0.5f;
						}
					}
				}
				for (int i = first; i < end && !a_spheres.empty(); i++) {
					if ((s.topPinned && Length(s.pos[i] - a) < P.clearance) || (s.bottomPinned && Length(s.pos[i] - b) < P.clearance))
						continue;
					if (s.kind == 1 && s.age < 0.15f)
						continue;   // a shot leaves his own body first
					const NiPoint3 before = s.pos[i];
					bool moved = false;
					NiAVObject* hitNode = nullptr;
					for (const Ball& ball : a_spheres) {
						const Sphere* sp = ball.sphere;
						const NiPoint3 d = s.pos[i] - sp->worldPos;
						const float dist = Length(d);
						const float limit = (float)sp->radius + s.radius + kSkinMargin;
						if (dist >= limit)
							continue;
						s.pos[i] = sp->worldPos + (dist > 1e-4f ? d * (1.0f / dist) : NiPoint3(0, 0, 1)) * limit;
						moved = true;
						hitNode = ball.node;
					}
					// a shot's free end sticks where it lands, fixed in that body part's node from now on
					if (moved && flying && i == last && hitNode && !s.bottomPinned) {
						s.bottom = End::At(hitNode, s.pos[i]);
						s.bottomPinned = true;
						s.landed = true;
						s.age = (std::min)(s.age, 1.0f);
					}
					if (moved) {
						s.prev[i] = s.prev[i] + (s.pos[i] - before);
						s.prev[i] = s.prev[i] + (s.pos[i] - s.prev[i]) * kSkinFriction;
					}
				}
				const float flow = (std::min)(1.0f, P.viscosity * h);
				for (int k = 0; k < last; k++) {
					const float l = Length(s.pos[k + 1] - s.pos[k]);
					if (l > s.rest[k])
						s.rest[k] += (l - s.rest[k]) * flow;
				}
			}
			float total = 0.0f;
			for (int k = 0; k < last; k++)
				if (k != s.breakAt)
					total += Length(s.pos[k + 1] - s.pos[k]);
			s.age += a_delta;
			if (s.kind == 0) {
				s.radius = std::sqrt(s.volume / (kPi * (std::max)(total, 0.01f)));
				if (s.breakAt < 0 && (s.radius < P.minRadius || total - s.startLength > P.maxLength || s.age > P.life))
					Snap(s, P.fade);
			} else {
				s.radius = s.fixedRadius;
				if (s.kind == 2 && s.topPinned && (total > P.dripMax || s.age > P.dripLife))
					s.topPinned = false;   // too long or too old: the drip lets go of her and falls
				const bool done = s.kind == 1 ? (s.landed ? s.age > P.shotLife : s.age > 1.6f)   // landed / missed every body
				                              : !s.topPinned && s.age > P.dripLife + 0.8f;
				if (s.fadeRate <= 0.0f && done)
					s.fadeRate = 1.0f / (std::max)(P.fade, 0.05f);
			}
			if (s.breakAt >= 0 || (s.kind != 0 && s.fadeRate > 0.0f))
				s.fade -= s.fadeRate * a_delta;
		}

		// OStrands' Pose: each bone at its point along the rope, its scale the radius (thicker at the ends, gone where it
		// broke, faded, grown in over kFadeIn); local and world written, then the engine's own update down our tree
		void Pose(Strand& s)
		{
			const NiTransform& pw = G::World(s.attachedTo);
			const int last = s.n - 1;
			NiPoint3 lo = s.pos[0], hi = s.pos[0];
			for (int i = 0; i <= last; i++) {
				const float t = (float)i / last;
				float r;
				if (s.kind == 2)        // OStrands' drip: a bead at the bottom
					r = s.radius * (i == last ? 2.2f : i == last - 1 ? 1.4f : 1.0f);
				else if (s.kind == 1)   // a stream: fuller at its leading end
					r = s.radius * (0.7f + 0.6f * t);
				else
					r = s.radius * (1.0f + 0.6f * (1.0f - std::sin(kPi * t)));
				if (s.breakAt >= 0 && (i == s.breakAt || i == s.breakAt + 1))
					r = 0.0f;
				r *= (std::max)(s.fade, 0.0f) * (std::min)(s.age / kFadeIn, 1.0f);
				NiPoint3 dir;
				if (s.breakAt >= 0 && i == s.breakAt)
					dir = s.pos[i] - s.pos[i - (i > 0)];   // across the break the rope has no next point
				else
					dir = i < last ? s.pos[i + 1] - s.pos[i] : s.pos[i] - s.pos[i - 1];
				Place(s.bones[i], Frame(s.pos[i], dir, (std::max)(r, 0.0005f)), pw);
				lo = NiPoint3((std::min)(lo.x, s.pos[i].x), (std::min)(lo.y, s.pos[i].y), (std::min)(lo.z, s.pos[i].z));
				hi = NiPoint3((std::max)(hi.x, s.pos[i].x), (std::max)(hi.y, s.pos[i].y), (std::max)(hi.z, s.pos[i].z));
			}
			G::World(s.root.get()) = pw;
			RE::NiUpdateData update{};
			s.root->UpdateDownwardPass(update, 0);
			// the bounds by hand: the shape's model bound is around the rest pose
			const NiPoint3 c = (lo + hi) * 0.5f;
			const float br = Length(hi - lo) * 0.5f + 1.0f;
			for (UInt16 i = 0; i < G::ChildCount(s.root.get()); i++)
				if (NiAVObject* sh = G::Child(s.root.get(), i); sh && !G::AsNode(sh)) {
					sh->worldBound.center.x = c.x, sh->worldBound.center.y = c.y, sh->worldBound.center.z = c.z;
					sh->worldBound.fRadius = br;
				}
			s.root->worldBound.center.x = c.x, s.root->worldBound.center.y = c.y, s.root->worldBound.center.z = c.z;
			s.root->worldBound.fRadius = br;
		}

		void Note(const char* a_what, UInt32 a_owner, int a_kind, UInt32 a_partner, float a_length)
		{
			if (logged >= 40)
				return;
			logged++;
			spdlog::info("[strands] {}: {:08X} (opening {}) and {:08X}, {:.1f} apart", a_what, a_owner, a_kind, a_partner, a_length);
		}

		// OStrands' SpawnBridges: where his shaft crosses her entrance's plane, 1-2 strands from points on her rim to the
		// shaft's surface there, each end fixed in its own node (her pelvis, his shaft bone) so they part as he pulls back
		void SpawnBridges(PairState& st, const AimOpening& o, Actor* a_her, Actor* a_him, NiNode* a_parent)
		{
			std::vector<NiAVObject*> chain;
			NiAVObject* herNode = OpeningNode(a_her, o.kind);
			if (!herNode || !AimChain(a_him, chain) || chain.size() < 2)
				return;
			const NiPoint3 in = Unit(o.in, NiPoint3(0, 1, 0));
			auto depth = [&](const NiPoint3& p) { return Dot(p - o.point, in); };
			std::vector<NiPoint3> pts;
			for (NiAVObject* n : chain)
				pts.push_back(G::World(n).pos);
			int segment = -1;
			float t = 0.0f;
			for (size_t i = 0; i + 1 < pts.size(); i++) {
				const float d0 = depth(pts[i]), d1 = depth(pts[i + 1]);
				if (d0 <= 0.0f && d1 > 0.0f) {
					segment = (int)i;
					t = d0 / (d0 - d1);
					break;
				}
			}
			if (segment < 0)
				return;
			st.cooldown = P.cooldown;
			const NiPoint3 crossing = pts[segment] + (pts[segment + 1] - pts[segment]) * t;
			const NiPoint3 axis = Unit(pts[segment + 1] - pts[segment], in);
			const std::uint64_t key = ((std::uint64_t)o.owner << 2) | (unsigned)o.kind;
			int count = 1 + (Random01() < 0.4f ? 1 : 0);
			count = (std::min)({ count, (int)st.budget, P.maxPerStroke, P.maxPerPair - Count(key) });
			// two axes across the opening, for points on its rim
			const NiPoint3 u = Unit(Cross(in, std::fabs(in.z) < 0.9f ? NiPoint3(0, 0, 1) : NiPoint3(1, 0, 0)));
			const NiPoint3 v = Cross(in, u);
			for (int n = 0; n < count; n++) {
				Strand* s = Acquire();
				if (!s)
					return;
				const float angle = Random01() * 2.0f * kPi;
				const NiPoint3 rim = o.point + (u * std::cos(angle) + v * std::sin(angle)) * P.rimRadius;
				NiPoint3 radial = rim - crossing;
				radial = radial - axis * Dot(radial, axis);
				const NiPoint3 shaftPoint = crossing + Unit(radial, u) * P.shaftRadius;
				Start(*s, o.owner, a_him->formID, key, End::At(herNode, rim), End::At(chain[segment], shaftPoint), a_parent);
				st.budget -= 1.0f;
				Note("a strand on the outstroke", o.owner, o.kind, a_him->formID, s->startLength);
			}
		}

		// the pull-out: the shaft left; one strand from her entrance to his tip
		void SpawnPullout(const AimOpening& o, UInt32 a_partner, Actor* a_her, NiNode* a_parent)
		{
			Actor* him = ActorOf(a_partner);
			std::vector<NiAVObject*> chain;
			NiAVObject* herNode = OpeningNode(a_her, o.kind);
			if (!him || !herNode || !AimChain(him, chain) || chain.empty())
				return;
			const NiPoint3 tip = G::World(chain.back()).pos;
			const float gap = Length(tip - o.point);
			if (gap >= P.maxStart || Random01() >= P.pulloutChance)
				return;
			if (Strand* s = Acquire()) {
				const std::uint64_t key = ((std::uint64_t)o.owner << 2) | (unsigned)o.kind;
				Start(*s, o.owner, a_partner, key, End::At(herNode, o.point), End::At(chain.back(), tip), a_parent);
				Note("a strand on the pull-out", o.owner, o.kind, a_partner, gap);
			}
		}

		// ---- climax (2026-10-16): the queue from Papyrus (AnatomyAim.Climax), shots and drips ----
		std::mutex climaxLock;              // Papyrus' thread queues, the scan's Tick drains
		std::vector<std::vector<UInt32>> climaxQueue;
		std::unordered_map<UInt32, float> lastClimax;   // his formID -> seconds since; one climax per cooldown
		struct Emitter
		{
			UInt32 him = 0, her = 0;
			int left = 0;
			float next = 0.0f, speed = 0.0f;
		};
		std::vector<Emitter> emitters;
		struct Reservoir
		{
			UInt32 her = 0;
			int kind = 0;
			int drips = 0;
			float next = 0.0f, age = 0.0f;
		};
		std::vector<Reservoir> reservoirs;

		// one climax: each actor of the stage with a shaft chain; inside an opening Aim holds, drips from it after he is
		// out; outside, pulses of shots from his tip toward whoever is with him
		void StartClimax(const std::vector<UInt32>& a_actors)
		{
			for (UInt32 id : a_actors) {
				Actor* him = ActorOf(id);
				std::vector<NiAVObject*> chain;
				if (!him || !AimChain(him, chain) || chain.size() < 2)
					continue;
				auto seen = lastClimax.find(id);
				if (seen != lastClimax.end() && seen->second < P.climaxCooldown)
					continue;
				lastClimax[id] = 0.0f;
				UInt32 other = 0;
				for (UInt32 o : a_actors)
					if (o != id)
						other = o;
				if (Fluid::Ready()) {   // the climax fluid (Fluid.h): the opening he is in, if any, and who is with him
					UInt32 her = 0;
					int kind = -1;
					float best = 1.5f;   // of two openings he held within the moment, the one held last (10-17: a mouth
					                     // scene read as the anus the aim had brushed a second before)
					for (auto& [key, st] : pairs)
						if (st.lastPartner == id && st.since < best) {
							her = (UInt32)(key >> 2);
							kind = (int)(key & 3);
							best = st.since;
						}
					Fluid::Climax(id, her ? her : other, her, kind);
					Note(her ? "a climax inside (fluid)" : "a climax outside (fluid)", her ? her : other, kind, id, 0.0f);
					continue;
				}
				bool inside = false;
				for (auto& [key, st] : pairs)
					if (st.lastPartner == id && st.since < 1.5f) {
						reservoirs.push_back(Reservoir{ (UInt32)(key >> 2), (int)(key & 3), P.drips, 0.6f, 0.0f });
						inside = true;
						Note("a climax inside", (UInt32)(key >> 2), (int)(key & 3), id, 0.0f);
						break;
					}
				if (!inside) {
					const int span = (std::max)(0, P.shotsMax - P.shotsMin);
					const int pulses = P.shotsMin + (int)(Random01() * (span + 0.999f));
					emitters.push_back(Emitter{ id, other, pulses, 0.0f, P.shotSpeed });
					Note("a climax outside", other, -1, id, (float)pulses);
				}
			}
		}

		void SpawnShot(Emitter& e)
		{
			Actor* him = ActorOf(e.him);
			std::vector<NiAVObject*> chain;
			NiNode* parent = him ? G::AsNode(G::Root(him)) : nullptr;
			if (!parent || !AimChain(him, chain) || chain.size() < 2)
				return;
			const NiPoint3 tip = G::World(chain.back()).pos;
			NiPoint3 dir = Unit(tip - G::World(chain[chain.size() - 2]).pos);
			const NiPoint3 jitter(Random01() - 0.5f, Random01() - 0.5f, Random01() - 0.5f);
			dir = Unit(dir + jitter * (2.0f * P.shotSpread) + NiPoint3(0, 0, 0.1f));
			if (Strand* s = Acquire()) {
				StartFree(*s, 1, e.him, e.her, End::At(chain.back(), tip), dir * e.speed, P.shotRadius, parent);
				Note("a shot", e.her, -1, e.him, e.speed);
			}
		}

		void SpawnDrip(const Reservoir& r)
		{
			Actor* her = ActorOf(r.her);
			NiNode* parent = her ? G::AsNode(G::Root(her)) : nullptr;
			NiAVObject* node = her ? OpeningNode(her, r.kind) : nullptr;
			if (!parent || !node)
				return;
			for (const AimOpening& o : AimOpenings())
				if (o.owner == r.her && o.kind == r.kind) {
					if (Strand* s = Acquire()) {
						StartFree(*s, 2, r.her, 0, End::At(node, o.point), NiPoint3(0, 0, -8.0f), P.dripRadius, parent);
						Note("a drip", r.her, r.kind, 0, 0.0f);
					}
					return;
				}
		}

		void TickClimax(float dt)
		{
			std::vector<std::vector<UInt32>> queued;
			{
				std::lock_guard<std::mutex> l(climaxLock);
				queued.swap(climaxQueue);
			}
			for (auto& c : lastClimax)
				c.second += dt;
			if (P.climax)
				for (auto& q : queued)
					StartClimax(q);
			for (Emitter& e : emitters) {
				e.next -= dt;
				if (e.left > 0 && e.next <= 0.0f) {
					SpawnShot(e);
					e.left--;
					e.next = P.shotInterval * (0.8f + 0.4f * Random01());
					e.speed *= 0.8f;
				}
			}
			emitters.erase(std::remove_if(emitters.begin(), emitters.end(), [](const Emitter& e) { return e.left <= 0; }),
				emitters.end());
			for (Reservoir& r : reservoirs) {
				r.age += dt;
				auto st = pairs.find(((std::uint64_t)r.her << 2) | (unsigned)r.kind);
				if (st != pairs.end() && (st->second.partner || st->second.since < 1.0f))
					continue;   // he is still in her (or was a moment ago: the lock flickers between strokes): it waits
				r.next -= dt;
				if (r.drips > 0 && r.next <= 0.0f) {
					SpawnDrip(r);
					r.drips--;
					r.next = P.dripInterval * (0.7f + 0.6f * Random01());
				}
			}
			reservoirs.erase(std::remove_if(reservoirs.begin(), reservoirs.end(),
								 [](const Reservoir& r) { return r.drips <= 0 || r.age > 120.0f; }),
				reservoirs.end());
		}

		void TickImpl()
		{
			std::lock_guard<std::mutex> guard(lock);
			if (!P.enabled) {   // switched off (or an exception) with strands up: they go
				for (Strand& s : pool)
					if (s.active)
						Retire(s);
				return;
			}
			LARGE_INTEGER now, freq;
			QueryPerformanceCounter(&now);
			QueryPerformanceFrequency(&freq);
			float dt = lastTick.QuadPart ? (float)(now.QuadPart - lastTick.QuadPart) / (float)freq.QuadPart : 0.0f;
			lastTick = now;
			dt = (std::min)((std::max)(dt, 0.0f), 0.05f);
			if (dt <= 0.0f)
				return;
			frame++;

			// every opening and what holds it. Not only those in a scene: Aim locks a shaft only in one, and the last
			// pull-out is as the scene ends, when AAF's busy flag may already be gone (10-16)
			for (const AimOpening& o : AimOpenings()) {
				if (o.kind < 0 || o.kind > 2)
					continue;
				const std::uint64_t key = ((std::uint64_t)o.owner << 2) | (unsigned)o.kind;
				PairState& st = pairs[key];
				st.seen = frame;
				const UInt32 partner = o.kind == 2 ? AimOralPartner(o.owner)
				                                   : ((AimReceivedKinds(o.owner) >> o.kind) & 1u) ? AimPartner(o.owner) : 0u;
				Actor* her = ActorOf(o.owner);
				NiNode* parent = her ? G::AsNode(G::Root(her)) : nullptr;
				if (!parent)
					continue;
				if (partner) {
					st.lastPartner = partner;
					st.since = 0.0f;
				} else
					st.since += dt;
				if (!partner) {
					if (st.partner && st.partner != o.owner)
						SpawnPullout(o, st.partner, her, parent);
					st.partner = 0;
					st.stroke = Swing{};
					st.bridgePending = false;
					continue;
				}
				st.partner = partner;
				if (!P.bridges || partner == o.owner)
					continue;
				// OStrands' TrackStroke: the depth's swing; Left (pulling back) arms a stroke's strands, made while the
				// shaft is still inside, once the cooldown and the budget allow
				const float depth = o.kind == 2 ? AimOralDepth(o.owner) : AimDepth(o.owner);
				st.cooldown -= dt;
				st.budget = (std::min)(st.budget + P.bridgeRate * dt, kMaxBudget);
				const Crossing c = st.stroke.Step(-depth, dt);
				if (c == Crossing::kArrived)
					st.bridgePending = false;
				else if (c == Crossing::kLeft)
					st.bridgePending = true;
				if (!st.bridgePending || st.cooldown > 0.0f || st.budget < 1.0f)
					continue;
				st.bridgePending = false;
				if (depth > 0.0f)
					if (Actor* him = ActorOf(partner))
						SpawnBridges(st, o, her, him, parent);
			}
			TickClimax(dt);   // before pairs are pruned: a climax reads who holds which opening this frame
			for (auto it = pairs.begin(); it != pairs.end();)
				it = it->second.seen != frame ? pairs.erase(it) : std::next(it);

			std::vector<Ball> spheres;
			for (Strand& s : pool) {
				if (!s.active)
					continue;
				Actor* host = ActorOf(s.owner);
				NiNode* parent = host ? G::AsNode(G::Root(host)) : nullptr;
				Actor* other = s.partner ? ActorOf(s.partner) : nullptr;
				// its carrier's 3D replaced or gone, the partner a bridge needs gone, or a fixed end's node no longer in a
				// tree: the strand goes
				if (!parent || parent != s.attachedTo || (s.kind == 0 && (!other || !G::Root(other))) ||
					(s.topPinned && (!s.top.node || !s.top.node->parent)) ||
					(s.bottomPinned && (!s.bottom.node || !s.bottom.node->parent))) {
					Retire(s);
					continue;
				}
				Spheres(host, other, spheres);
				Simulate(s, dt, spheres);
				if ((s.breakAt >= 0 || s.kind != 0) && s.fade <= 0.0f) {
					Retire(s);
					continue;
				}
				Pose(s);
			}
		}
	}

	void LoadConfig(INIReader& reader)
	{
		P = Params();
		P.enabled = reader.GetBoolean("Strands", "enabled", true);
		P.bridges = reader.GetBoolean("Strands", "bridges", true);
		auto f = [&](const char* k, float d) { return (float)reader.GetReal("Strands", k, d); };
		auto i = [&](const char* k, int d) { return (int)reader.GetInteger("Strands", k, d); };
		P.bridgeRate = (std::max)(0.0f, f("bridgeRate", P.bridgeRate));
		P.maxPerStroke = (std::clamp)(i("maxPerStroke", P.maxPerStroke), 0, kPool);
		P.maxPerPair = (std::clamp)(i("maxPerPair", P.maxPerPair), 0, kPool);
		P.cooldown = (std::max)(0.0f, f("cooldown", P.cooldown));
		P.pulloutChance = (std::clamp)(f("pulloutChance", P.pulloutChance), 0.0f, 1.0f);
		P.maxStart = (std::max)(1.0f, f("maxStart", P.maxStart));
		P.shaftRadius = (std::max)(0.0f, f("shaftRadius", P.shaftRadius));
		P.rimRadius = (std::max)(0.0f, f("rimRadius", P.rimRadius));
		P.radius = (std::max)(0.01f, f("radius", P.radius));
		P.minRadius = (std::max)(0.001f, f("minRadius", P.minRadius));
		P.maxLength = (std::max)(1.0f, f("maxLength", P.maxLength));
		P.gravity = f("gravity", P.gravity);
		P.damping = (std::clamp)(f("damping", P.damping), 0.0f, 1.0f);
		P.viscosity = (std::max)(0.0f, f("viscosity", P.viscosity));
		P.life = (std::max)(0.5f, f("life", P.life));
		P.fade = (std::max)(0.01f, f("fade", P.fade));
		P.collide = reader.GetBoolean("Strands", "collide", true);
		P.climax = reader.GetBoolean("Strands", "climax", true);
		P.shotsMin = (std::clamp)(i("shotsMin", P.shotsMin), 0, kPool);
		P.shotsMax = (std::clamp)(i("shotsMax", P.shotsMax), P.shotsMin, kPool);
		P.shotInterval = (std::max)(0.05f, f("shotInterval", P.shotInterval));
		P.shotSpeed = (std::max)(0.0f, f("shotSpeed", P.shotSpeed));
		P.shotSpread = (std::max)(0.0f, f("shotSpread", P.shotSpread));
		P.shotGravity = f("shotGravity", P.shotGravity);
		P.shotRadius = (std::max)(0.01f, f("shotRadius", P.shotRadius));
		P.shotRelease = (std::max)(0.0f, f("shotRelease", P.shotRelease));
		P.shotLife = (std::max)(0.5f, f("shotLife", P.shotLife));
		P.drips = (std::clamp)(i("drips", P.drips), 0, 50);
		P.dripInterval = (std::max)(0.05f, f("dripInterval", P.dripInterval));
		P.dripRadius = (std::max)(0.01f, f("dripRadius", P.dripRadius));
		P.dripStretch = (std::max)(0.05f, f("dripStretch", P.dripStretch));
		P.dripMax = (std::max)(0.5f, f("dripMax", P.dripMax));
		P.dripLife = (std::max)(0.5f, f("dripLife", P.dripLife));
		P.climaxCooldown = (std::max)(0.0f, f("climaxCooldown", P.climaxCooldown));
		P.clearance = (std::max)(0.0f, f("clearance", P.clearance));
	}

	void Tick()
	{
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

	int InsideOf(unsigned int a_him)
	{
		// Papyrus' thread: the scan's Tick holds the lock for a frame's work, so a busy lock answers what was last seen
		static std::mutex cacheLock;
		static std::unordered_map<UInt32, int> cache;
		std::unique_lock<std::mutex> guard(lock, std::try_to_lock);
		if (!guard.owns_lock()) {
			std::lock_guard<std::mutex> l(cacheLock);
			auto it = cache.find(a_him);
			return it != cache.end() ? it->second : -1;
		}
		int kind = -1;
		float best = 1.5f;   // the climax's own test (StartClimax), and of two openings the one he held last
		for (auto& [key, st] : pairs)
			if (st.lastPartner == a_him && st.since < best)
				kind = (int)(key & 3), best = st.since;
		std::lock_guard<std::mutex> l(cacheLock);
		cache[a_him] = kind;
		return kind;
	}

	void Climax(const std::vector<unsigned int>& a_actors)
	{
		std::lock_guard<std::mutex> l(climaxLock);
		if (climaxQueue.size() < 16)
			climaxQueue.push_back(a_actors);
	}

	void Reset()
	{
		{
			std::lock_guard<std::mutex> l(climaxLock);
			climaxQueue.clear();
		}
		std::lock_guard<std::mutex> guard(lock);
		emitters.clear();
		reservoirs.clear();
		lastClimax.clear();
		for (Strand& s : pool)
			if (s.active || s.attachedTo)
				Retire(s);
		pairs.clear();
	}
}
