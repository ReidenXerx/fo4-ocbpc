// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-16 (see Strands.h; the design is OStrands').
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Strands.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

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
		constexpr int kPool = 8;   // meshes\Anatomy\Strand\Strand00..07.nif: one Demand prototype each (no clone yet)
		constexpr float kPi = 3.14159265f;
		// OStrands' StrandManager.cpp constants
		constexpr int kSubsteps = 2;
		constexpr int kIterations = 6;
		constexpr float kMaxBudget = 3.0f;
		constexpr float kFadeIn = 0.12f;
		constexpr float kSwingRelax = 0.1f;
		constexpr float kMinSwing = 0.6f;
		constexpr float kStrokeNear = 0.1f, kStrokeNearMax = 0.6f, kStrokeGap = 0.1f, kStrokeGapMax = 0.6f;

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
			std::array<NiAVObject*, kBones> bones{};
			NiNode* attachedTo = nullptr;
			bool failed = false;
			// the strand
			bool active = false;
			UInt32 owner = 0;            // her: whose 3D carries it
			UInt32 partner = 0;
			std::uint64_t pair = 0;      // owner << 2 | opening kind
			End top, bottom;
			std::array<NiPoint3, kBones> pos{}, prev{};
			std::array<float, kBones - 1> rest{};
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
			for (int i = 0; i < kBones; i++) {
				char name[16];
				std::snprintf(name, sizeof(name), "Strand%02d", i);
				s.bones[i] = G::Find(model.get(), name);
				if (!s.bones[i]) {
					spdlog::info("[strands] meshes\\{} has no {}: that strand stays off", path, name);
					return false;
				}
			}
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
			s.owner = a_owner;
			s.partner = a_partner;
			s.pair = a_pair;
			s.top = std::move(a_top);
			s.bottom = std::move(a_bottom);
			for (int i = 0; i < kBones; i++)
				s.pos[i] = s.prev[i] = a + (b - a) * ((float)i / (kBones - 1));
			s.rest.fill((std::max)(length / (kBones - 1), 0.02f));
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

		void Snap(Strand& s, float a_fadeTime)
		{
			if (s.breakAt >= 0)
				return;
			const int mid = kBones / 2 - 2 + (int)(Random01() * 3.0f);   // OStrands: kBones / 2 - 2 .. kBones / 2
			s.breakAt = (std::clamp)(mid, 0, kBones - 2);
			s.fadeRate = 1.0f / (std::max)(a_fadeTime, 0.05f);
		}

		// OStrands' Simulate without the body collision: substeps of verlet under gravity, the ends pinned, a rope's
		// constraints (none across a break), and viscosity: a stretched segment's rest length flows after it
		void Simulate(Strand& s, float a_delta)
		{
			const NiPoint3 a = s.top.World(), b = s.bottom.World();
			const NiPoint3 gravity(0, 0, -P.gravity);
			const float h = a_delta / kSubsteps;
			const float damping = h > 0.0f ? std::pow((std::clamp)(P.damping, 0.0f, 1.0f), h * 120.0f) : 1.0f;
			constexpr int last = kBones - 1;
			for (int step = 0; step < kSubsteps; step++) {
				const float scale = damping * (s.previousStep > 0.0f ? h / s.previousStep : 1.0f);
				for (int i = 1; i < last; i++) {
					const NiPoint3 v = (s.pos[i] - s.prev[i]) * scale;
					s.prev[i] = s.pos[i];
					s.pos[i] = s.pos[i] + v + gravity * (h * h);
				}
				s.previousStep = h;
				s.pos[0] = a;
				s.pos[last] = b;
				for (int it = 0; it < kIterations; it++) {
					for (int k = 0; k < last; k++) {
						if (k == s.breakAt)
							continue;
						const NiPoint3 d = s.pos[k + 1] - s.pos[k];
						const float l = Length(d);
						if (l < 1e-5f)
							continue;
						const NiPoint3 fix = d * ((l - s.rest[k]) / l);
						if (k == 0)
							s.pos[k + 1] = s.pos[k + 1] - fix;
						else if (k + 1 == last)
							s.pos[k] = s.pos[k] + fix;
						else {
							s.pos[k] = s.pos[k] + fix * 0.5f;
							s.pos[k + 1] = s.pos[k + 1] - fix * 0.5f;
						}
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
			s.radius = std::sqrt(s.volume / (kPi * (std::max)(total, 0.01f)));
			s.age += a_delta;
			if (s.breakAt < 0 && (s.radius < P.minRadius || total - s.startLength > P.maxLength || s.age > P.life))
				Snap(s, P.fade);
			if (s.breakAt >= 0)
				s.fade -= s.fadeRate * a_delta;
		}

		// OStrands' Pose: each bone at its point along the rope, its scale the radius (thicker at the ends, gone where it
		// broke, faded, grown in over kFadeIn); local and world written, then the engine's own update down our tree
		void Pose(Strand& s)
		{
			const NiTransform& pw = G::World(s.attachedTo);
			constexpr int last = kBones - 1;
			NiPoint3 lo = s.pos[0], hi = s.pos[0];
			for (int i = 0; i <= last; i++) {
				const float t = (float)i / last;
				float r = s.radius * (1.0f + 0.6f * (1.0f - std::sin(kPi * t)));
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
			for (auto it = pairs.begin(); it != pairs.end();)
				it = it->second.seen != frame ? pairs.erase(it) : std::next(it);

			for (Strand& s : pool) {
				if (!s.active)
					continue;
				Actor* her = ActorOf(s.owner);
				NiNode* parent = her ? G::AsNode(G::Root(her)) : nullptr;
				Actor* him = ActorOf(s.partner);
				// her 3D replaced or gone, his gone, or an end's node no longer in a tree: the strand goes
				if (!parent || parent != s.attachedTo || !him || !G::Root(him) || !s.top.node || !s.top.node->parent ||
					!s.bottom.node || !s.bottom.node->parent) {
					Retire(s);
					continue;
				}
				Simulate(s, dt);
				if (s.breakAt >= 0 && s.fade <= 0.0f) {
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

	void Reset()
	{
		std::lock_guard<std::mutex> guard(lock);
		for (Strand& s : pool)
			if (s.active || s.attachedTo)
				Retire(s);
		pairs.clear();
	}
}
