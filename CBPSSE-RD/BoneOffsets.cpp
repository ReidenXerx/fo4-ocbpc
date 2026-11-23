// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-11-23: per-actor bone translation offsets after the animation.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "BoneOffsets.h"

#include "Bones.h"
#include "CollisionHub.h"
#include "Game.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	void Note(const std::string& key, const char* fmt, ...)
	{
		char line[512];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
		va_end(args);
		AnatomyLogLine(key, line);
	}

	NiMatrix43 Turn(int axis, float degrees);

	struct Offset
	{
		std::string bone;
		bool scale = false;      // *k: the animated local translation times k; else + d
		float k = 1.0f;
		NiPoint3 d;
		bool rotate = false;     // [Rotations]: the animated local rotation turned by r about the bone's own axes
		NiMatrix43 r;
	};

	struct Spec
	{
		std::string file, plugin;
		std::uint32_t local = 0;     // the NPC_ base form, local to the plugin
		std::uint32_t npc = 0;       // its runtime form id, once the plugin's forms are there
		int nextTry = 0;
		std::vector<Offset> offsets;
		std::vector<std::string> logBones;   // [Log] bones=...: their positions after the offsets, sampled
		float logEvery = 1.0f;               // seconds between samples
		bool logRot = false;                 // rotations=1: each bone's rotation in the skeleton's space too
		int logSamples = 30;
	};

	struct BoneState
	{
		NiAVObject* node = nullptr;
		bool top = false;            // no other offset bone above it: its subtree's world is redone from here
		bool valid = false;
		NiPoint3 base, written;      // the pose's value last frame, and what we wrote over it
		NiMatrix43 baseRot, writtenRot;
		bool modelValid = false;
		NiPoint3 modelWritten;       // where our world write put it, in the skeleton's space
	};

	struct ActorState
	{
		NiAVObject* root = nullptr;
		std::vector<BoneState> bones;
		std::vector<NiAVObject*> logNodes;
		int logged = 0;
		ULONGLONG nextLog = 0;
		std::uint64_t lastFrame = 0;         // the frame of the last sample
		int keyed = 0, moved = 0;            // this frame: bones the animation set anew, of the bones we move
		int worldMoved = 0;                  // ... and bones whose world moved since our write while the local did not
		double costSum = 0.0, costMax = 0.0; // microseconds of our work on her since the last sample
		int costFrames = 0;
		std::uint64_t hookFrame = 0;         // the frame the animation-update hook last laid her offsets on
		int byHook = 0, byFrame = 0;         // since the last sample: applied by that hook, by the frame pass
	};

	std::mutex lock;
	std::vector<Spec> specs;
	std::unordered_map<std::uint32_t, int> matched;          // actor form id -> spec index, -1 none
	std::unordered_map<std::uint32_t, ActorState> states;

	std::string Trim(std::string s)
	{
		const char* ws = " \t\r\n";
		s.erase(0, s.find_first_not_of(ws));
		const size_t e = s.find_last_not_of(ws);
		s.erase(e == std::string::npos ? 0 : e + 1);
		return s;
	}

	bool ParseOffset(const std::string& key, const std::string& value, Offset& out)
	{
		out.bone = key;
		if (!value.empty() && value[0] == '*') {
			out.scale = true;
			out.k = std::strtof(value.c_str() + 1, nullptr);
			return out.k > 0.0f;
		}
		float v[3];
		if (sscanf_s(value.c_str(), " %f , %f , %f", &v[0], &v[1], &v[2]) != 3)
			return false;
		out.d = NiPoint3(v[0], v[1], v[2]);
		return true;
	}

	bool ReadFile(const std::filesystem::path& path, Spec& s)
	{
		std::ifstream in(path);
		if (!in)
			return false;
		s.file = path.filename().string();
		std::string line, section;
		int bad = 0;
		while (std::getline(in, line)) {
			const size_t semi = line.find(';');
			if (semi != std::string::npos)
				line.erase(semi);
			line = Trim(line);
			if (line.empty())
				continue;
			if (line.front() == '[' && line.back() == ']') {
				section = Trim(line.substr(1, line.size() - 2));
				continue;
			}
			const size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			const std::string key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
			if (_stricmp(section.c_str(), "Actor") == 0) {
				if (_stricmp(key.c_str(), "plugin") == 0)
					s.plugin = value;
				else if (_stricmp(key.c_str(), "base") == 0)
					s.local = (std::uint32_t)std::strtoul(value.c_str(), nullptr, 0) & 0x00FFFFFF;
			} else if (_stricmp(section.c_str(), "Log") == 0) {
				if (_stricmp(key.c_str(), "bones") == 0) {
					size_t at = 0;
					while (at <= value.size()) {
						const size_t comma = value.find(',', at);
						const std::string name = Trim(value.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
						if (!name.empty() && s.logBones.size() < 32)
							s.logBones.push_back(name);
						if (comma == std::string::npos)
							break;
						at = comma + 1;
					}
				} else if (_stricmp(key.c_str(), "every") == 0) {
					s.logEvery = (std::max)(0.05f, std::strtof(value.c_str(), nullptr));
				} else if (_stricmp(key.c_str(), "rotations") == 0) {
					s.logRot = std::strtol(value.c_str(), nullptr, 10) != 0;
				} else if (_stricmp(key.c_str(), "samples") == 0) {
					s.logSamples = (std::min)(600, (std::max)(1, (int)std::strtol(value.c_str(), nullptr, 10)));
				}
			} else if (_stricmp(section.c_str(), "Rotations") == 0) {
				// Bone=x,y,z degrees, turned about the bone's own axes in that order: X first, then the turned Y,
				// then the twice-turned Z (o.r = Z * Y * X in the row layout)
				float v[3];
				if (sscanf_s(value.c_str(), " %f , %f , %f", &v[0], &v[1], &v[2]) == 3) {
					Offset o;
					o.bone = key;
					o.rotate = true;
					o.d = NiPoint3(v[0], v[1], v[2]);
					o.r = Turn(2, v[2]) * Turn(1, v[1]) * Turn(0, v[0]);
					s.offsets.push_back(o);
				} else {
					bad++;
				}
			} else if (_stricmp(section.c_str(), "Offsets") == 0) {
				Offset o;
				if (ParseOffset(key, value, o))
					s.offsets.push_back(o);
				else
					bad++;
			}
		}
		Note("offsets|file|" + s.file, "[offsets] %s: %s base %06X, %zu bones%s; %zu logged every %.2f s x %d\n", s.file.c_str(),
			s.plugin.c_str(), s.local, s.offsets.size(), bad ? " (some lines unreadable: [Offsets] Bone=*k or Bone=x,y,z, [Rotations] Bone=x,y,z degrees)" : "",
			s.logBones.size(), s.logEvery, s.logBones.empty() ? 0 : s.logSamples);
		// a file with only [Log] is fine: it samples the pose with nothing offset (the A/B baseline)
		return !s.plugin.empty() && s.local && (!s.offsets.empty() || !s.logBones.empty());
	}

	void Resolve(Spec& s)
	{
		if (s.npc || --s.nextTry > 0)
			return;
		s.nextTry = 600;             // about ten seconds: the plugin may simply not be in this load order
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data)
			return;
		if (auto* npc = data->LookupForm<RE::TESNPC>(s.local, s.plugin)) {
			s.npc = npc->formID;
			Note("offsets|npc|" + s.file, "[offsets] %s: %s %06X is NPC %08X\n", s.file.c_str(), s.plugin.c_str(),
				s.local, s.npc);
		} else {
			Note("offsets|missing|" + s.file, "[offsets] %s: no NPC %06X in %s (the plugin is not loaded?): nothing offset\n",
				s.file.c_str(), s.local, s.plugin.c_str());
		}
	}

	bool Above(NiAVObject* upper, NiAVObject* node)
	{
		for (int i = 0; node && i < 64; i++, node = G::Parent(node))
			if (node == upper)
				return true;
		return false;
	}

	// the same as Aim.cpp's UpdateWorldFrom: world from the parent's and the node's local, then down the children
	void WorldFrom(NiAVObject* node, const NiTransform& parent, int depth = 0)
	{
		if (!node || depth > 32)
			return;
		NiTransform& w = G::World(node);
		const NiTransform& l = G::Local(node);
		w.rot = l.rot * parent.rot;
		w.pos = parent.pos + parent.rot.Transpose() * (l.pos * parent.scale);
		w.scale = parent.scale * l.scale;
		if (NiNode* n = G::AsNode(node))
			for (UInt16 i = 0; i < G::ChildCount(n); i++)
				WorldFrom(G::Child(n, i), w, depth + 1);
	}

	bool Same(const NiPoint3& p, const NiPoint3& q) { return p.x == q.x && p.y == q.y && p.z == q.z; }

	bool SameRot(const NiMatrix43& p, const NiMatrix43& q)
	{
		for (int row = 0; row < 3; row++)
			for (int col = 0; col < 3; col++)
				if (p.data[row][col] != q.data[row][col])
					return false;
		return true;
	}

	// one turn of a degrees about the bone's own axis (0 X, 1 Y, 2 Z), right-handed, in the engine's row layout
	// (row k = where axis k points after the turn)
	NiMatrix43 Turn(int axis, float degrees)
	{
		const float rad = degrees * 0.01745329f, cs = std::cos(rad), sn = std::sin(rad);
		const int u = (axis + 1) % 3, v = (axis + 2) % 3;
		NiMatrix43 m;
		m.data[axis][axis] = 1.0f;
		m.data[u][u] = cs;
		m.data[u][v] = sn;           // + turns the next axis (Y for X) toward the one after (Z)
		m.data[v][u] = -sn;
		m.data[v][v] = cs;
		return m;
	}

	void Bind(Actor* actor, const Spec& s, ActorState& st, NiAVObject* root)
	{
		st.root = root;
		st.bones.assign(s.offsets.size(), BoneState{});
		std::string missing;
		int found = 0;
		for (size_t i = 0; i < s.offsets.size(); i++) {
			BSFixedString name(s.offsets[i].bone.c_str());
			st.bones[i].node = G::SkeletonNode(root, name);
			if (st.bones[i].node)
				found++;
			else if (missing.size() < 200)
				missing += " " + s.offsets[i].bone;
		}
		for (auto& bone : st.bones) {
			bone.top = bone.node != nullptr;
			for (auto& o : st.bones) {
				if (&o == &bone)
					break;           // a bone in both [Offsets] and [Rotations]: its subtree is redone once
				if (o.node == bone.node)
					bone.top = false;
			}
			for (auto& o : st.bones)
				if (bone.top && o.node && o.node != bone.node && Above(o.node, bone.node))
					bone.top = false;
		}
		st.logNodes.assign(s.logBones.size(), nullptr);
		for (size_t i = 0; i < s.logBones.size(); i++) {
			BSFixedString name(s.logBones[i].c_str());
			st.logNodes[i] = G::SkeletonNode(root, name);
		}
		char key[96];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "offsets|bind|%08X|%d", actor->formID, found);
		Note(key, "[offsets] %08X (%s): %d of %zu bones offset after the animation%s%s\n", actor->formID, s.file.c_str(),
			found, s.offsets.size(), missing.empty() ? "" : "; not in the skeleton:", missing.c_str());
	}

	// [Log]: where the named bones are once the offsets are in, both in the skeleton's own space (the frame a
	// nif's bind pose is in: model = root.rot * (world - root.pos) / root.scale) and in the world
	std::uint64_t frame = 0;     // our frame count: UpdateBoneOffsets once per game frame

	void SkinCheck(NiAVObject* obj, const Spec& s, const ActorState& st, int& ours, int& other, char* detail,
		size_t detailSize, int depth = 0);

	// Taken at the END of the engine's frame (SampleBoneOffsets), after the aim and the physics: a breast or belly
	// bone shows where OCBPC left it, every other bone where the offsets and the animation put it
	void LogSample(Actor* actor, const Spec& s, ActorState& st, NiAVObject* root)
	{
		if (s.logBones.empty() || st.logged >= s.logSamples)
			return;
		const ULONGLONG now = GetTickCount64();
		if (st.nextLog && now < st.nextLog)
			return;
		const ULONGLONG since = st.nextLog ? now - (st.nextLog - (ULONGLONG)(s.logEvery * 1000.0f)) : 0;
		st.nextLog = now + (ULONGLONG)(s.logEvery * 1000.0f);
		const NiTransform& r = G::World(root);
		const float inv = r.scale != 0.0f ? 1.0f / r.scale : 1.0f;
		const float heading = std::atan2(r.rot.data[1][0], r.rot.data[1][1]) * 57.29578f;
		const NiMatrix43 toModel = r.rot.Transpose();   // world.rot = model.rot * root.rot (rows = axes)
		int skinOurs = 0, skinOther = 0;
		char skinDetail[600] = "";
		SkinCheck(root, s, st, skinOurs, skinOther, skinDetail, sizeof(skinDetail));
		static char line[8192];
		int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
			"[offsets] %08X sample %d/%d: frame %llu (+%llu frames, +%llu ms); the animation set %d of the %d bones we move "
			"this frame%s, and %d moved in the world since our write; skin entries naming them: %d draw from our nodes, %d "
			"from other nodes%s%s; laid on by the animation hook %d times, by the frame pass %d; our cost %.1f us a pass "
			"(max %.1f) over %d passes | root (%.2f, %.2f, %.2f) heading %.1f scale %.3f |",
			actor->formID, st.logged + 1, s.logSamples, (unsigned long long)frame,
			(unsigned long long)(st.lastFrame ? frame - st.lastFrame : 0), (unsigned long long)since, st.keyed, st.moved,
			st.moved && !st.keyed ? " (no local re-animated)" : "", st.worldMoved, skinOurs, skinOther,
			skinOther ? ":" : "", skinDetail, st.byHook, st.byFrame, st.costFrames ? st.costSum / st.costFrames : 0.0,
			st.costMax, st.costFrames, r.pos.x, r.pos.y, r.pos.z, heading, r.scale);
		st.lastFrame = frame;
		st.costSum = st.costMax = 0.0;
		st.costFrames = 0;
		st.byHook = st.byFrame = 0;
		for (size_t i = 0; i < st.logNodes.size() && n > 0 && n < (int)sizeof(line) - 1; i++) {
			int add;
			if (NiAVObject* node = st.logNodes[i]) {
				const NiTransform& wt = G::World(node);
				const NiPoint3 w = wt.pos;
				const NiPoint3 m = (r.rot * (w - r.pos)) * inv;
				const NiPoint3 l = G::Local(node).pos;
				add = _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE,
					" %s model (%.2f, %.2f, %.2f) world (%.2f, %.2f, %.2f) local (%.3f, %.3f, %.3f)",
					s.logBones[i].c_str(), m.x, m.y, m.z, w.x, w.y, w.z, l.x, l.y, l.z);
				if (add >= 0 && s.logRot) {
					n += add;
					const NiMatrix43 mr = wt.rot * toModel;
					add = _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE,
						" rot [%.4f %.4f %.4f / %.4f %.4f %.4f / %.4f %.4f %.4f]", mr.data[0][0], mr.data[0][1], mr.data[0][2],
						mr.data[1][0], mr.data[1][1], mr.data[1][2], mr.data[2][0], mr.data[2][1], mr.data[2][2]);
				}
				if (add >= 0) {
					n += add;
					add = _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, ";");
				}
			} else {
				add = _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, " %s not in the skeleton;", s.logBones[i].c_str());
			}
			if (add < 0)
				break;
			n += add;
		}
		st.logged++;
		char key[64];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "offsets|sample|%08X|%d", actor->formID, st.logged);
		AnatomyLogLine(key, (std::string(line) + "\n").c_str());
	}

	void Apply(Actor* actor, const Spec& s, ActorState& st)
	{
		NiAVObject* root = G::Root(actor);
		if (!root)
			return;
		if (st.root != root || st.bones.size() != s.offsets.size())
			Bind(actor, s, st, root);   // a new 3D: the nodes are found again
		st.keyed = st.moved = st.worldMoved = 0;
		const NiTransform& r = G::World(root);
		const float inv = r.scale != 0.0f ? 1.0f / r.scale : 1.0f;
		for (size_t i = 0; i < st.bones.size(); i++) {
			BoneState& bone = st.bones[i];
			if (!bone.node)
				continue;
			// test12a: 37 of 40 samples saw no animation write to any local we move while she walked. Something that
			// writes the world without the local (a pose laid straight onto the world) would show here
			if (bone.modelValid) {
				const NiPoint3 now = (r.rot * (G::World(bone.node).pos - r.pos)) * inv;
				const NiPoint3 d = now - bone.modelWritten;
				if (d.x * d.x + d.y * d.y + d.z * d.z > 0.0001f)
					st.worldMoved++;
			}
			const Offset& o = s.offsets[i];
			if (o.rotate) {
				NiMatrix43& m = G::Local(bone.node).rot;
				const bool kept = bone.valid && SameRot(m, bone.writtenRot);
				st.moved++;
				st.keyed += kept ? 0 : 1;
				const NiMatrix43 base = kept ? bone.baseRot : m;
				// rows are the bone's axes in its parent's space: turning about its own axes multiplies from the left
				const NiMatrix43 out = o.r * base;
				for (int row = 0; row < 3; row++)
					for (int col = 0; col < 3; col++)
						m.data[row][col] = out.data[row][col];
				bone.baseRot = base;
				bone.writtenRot = m;
				bone.valid = true;
				continue;
			}
			NiPoint3& p = G::Local(bone.node).pos;
			// a bone the animation keys is rewritten every frame; one it leaves alone still holds what we wrote,
			// and gets it again from the kept pose value instead of piling the offset up frame after frame
			const bool kept = bone.valid && Same(p, bone.written);
			st.moved++;
			st.keyed += kept ? 0 : 1;
			const NiPoint3 base = kept ? bone.base : p;
			const NiPoint3 out = o.scale ? base * o.k : base + o.d;
			p = out;
			bone.base = base;
			bone.written = out;
			bone.valid = true;
		}
		for (auto& bone : st.bones)
			if (bone.top)
				if (NiNode* parent = G::Parent(bone.node))
					WorldFrom(bone.node, G::World(parent));
		for (auto& bone : st.bones) {
			if (!bone.node)
				continue;
			bone.modelWritten = (r.rot * (G::World(bone.node).pos - r.pos)) * inv;
			bone.modelValid = true;
		}
	}

	// Does what she wears draw from the nodes we move? Every skinned shape under her 3D, every skin entry naming one
	// of our bones: drawn from our node's world, or from another node of that name (a worn file's own copy)
	void SkinCheck(NiAVObject* obj, const Spec& s, const ActorState& st, int& ours, int& other, char* detail,
		size_t detailSize, int depth)
	{
		if (!obj || depth > 64)
			return;
		if (RE::BSGeometry* geo = G::AsGeometry(obj)) {
			G::SkinInstance* skin = G::Skin(geo);
			if (skin && G::SkinBones(skin).entries && G::SkinWorldTransforms(skin).entries) {
				UInt32 count = (std::min)(G::SkinBones(skin).count, G::SkinWorldTransforms(skin).count);
				for (UInt32 i = 0; i < count; i++) {
					NiNode* entry = G::SkinBones(skin).entries[i];
					const char* name = entry ? G::Name(entry) : nullptr;
					if (!name)
						continue;
					for (size_t k = 0; k < st.bones.size(); k++) {
						if (!st.bones[k].node || _stricmp(name, s.offsets[k].bone.c_str()) != 0)
							continue;
						if (G::SkinWorldTransforms(skin).entries[i] == &G::World(st.bones[k].node)) {
							ours++;
						} else {
							other++;
							const size_t used = strlen(detail);
							if (used + 80 < detailSize) {
								NiNode* parent = G::Parent(entry);
								_snprintf_s(detail + used, detailSize - used, _TRUNCATE, " %s:%s under '%s'", G::Name(geo),
									name, parent && G::Name(parent) ? G::Name(parent) : "-");
							}
						}
						break;
					}
				}
			}
			return;
		}
		if (NiNode* n = G::AsNode(obj))
			for (UInt16 i = 0; i < G::ChildCount(n); i++)
				SkinCheck(G::Child(n, i), s, st, ours, other, detail, detailSize, depth + 1);
	}

	bool SampleGuarded(Actor* actor, const Spec* s, ActorState* st, NiAVObject* root)
	{
		__try {
			LogSample(actor, *s, *st, root);
			return true;
		} __except (1) {
			return false;
		}
	}

	// A-51's rule: a fault while the 3D changes under us skips the actor this frame instead of taking the game down
	bool ApplyGuarded(Actor* actor, const Spec* s, ActorState* st)
	{
		__try {
			Apply(actor, *s, *st);
			return true;
		} __except (1) {
			return false;
		}
	}
}

void LoadBoneOffsets()
{
	std::vector<Spec> read;
	std::error_code ec;
	const std::filesystem::path dir("Data\\F4SE\\Plugins\\AnatomyBoneOffsets");
	if (std::filesystem::is_directory(dir, ec)) {
		for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
			if (!e.is_regular_file(ec) || _stricmp(e.path().extension().string().c_str(), ".ini") != 0)
				continue;
			Spec s;
			if (ReadFile(e.path(), s))
				read.push_back(std::move(s));
		}
	}
	std::lock_guard<std::mutex> guard(lock);
	specs = std::move(read);
	matched.clear();
	states.clear();
}

namespace
{
	// Ilham test12a/12b (2026-11-23): at the per-frame spot her 58 locals were rewritten by the animation only in runs
	// (40-61, 75-86 ... of 272 samples) while she walked the whole time, and nothing moved their world in between: the
	// NPC's animation runs on its own schedule, sometimes after that spot (weapon-mods measured the same, 11-14: a
	// bend written there flickered). So the offsets are laid on right after the game animates her: Actor's
	// UpdateAnimation, vtable slot 0x9F, hooked only where AE 1.11.240 was measured to hold it (Actor table rva 25683B0,
	// slot -> C67750) or where another plugin already chained it to live code. Anywhere else, or for an actor that
	// update never reaches, the frame pass stays the fallback.
	using AnimFn = void(RE::Actor*, float);
	std::atomic<AnimFn*> animate{ nullptr };
	std::atomic_bool anyMatched{ false };   // a loaded actor has an ini: the hook takes the lock only then
	bool hookTried = false;

	bool ApplyTimed(Actor* actor, std::uint32_t id, const Spec& s, ActorState& st)
	{
		LARGE_INTEGER t0, t1, hz;
		QueryPerformanceCounter(&t0);
		const bool ok = ApplyGuarded(actor, &s, &st);
		QueryPerformanceCounter(&t1);
		QueryPerformanceFrequency(&hz);
		if (ok) {
			const double us = (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)hz.QuadPart;
			st.costSum += us;
			st.costMax = (std::max)(st.costMax, us);
			st.costFrames++;
			return true;
		}
		states.erase(id);
		char key[64];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "offsets|fault|%08X", id);
		Note(key, "[offsets] %08X: a fault was caught; skipped this pass (her 3D was changing)\n", id);
		return false;
	}

	void Animated(RE::Actor* a_this, float a_delta)
	{
		if (AnimFn* original = animate.load(std::memory_order_acquire))
			original(a_this, a_delta);
		if (!a_this || !anyMatched.load(std::memory_order_relaxed))
			return;
		std::lock_guard<std::mutex> guard(lock);
		const std::uint32_t id = a_this->formID;
		auto m = matched.find(id);
		if (m == matched.end() || m->second < 0 || m->second >= (int)specs.size() || !G::Root(a_this))
			return;
		ActorState& st = states[id];
		if (ApplyTimed(a_this, id, specs[m->second], st)) {
			st.hookFrame = frame;
			st.byHook++;
		}
	}

	void InstallHook(Actor* npc)
	{
		hookTried = true;
		const std::uintptr_t base = REL::Module::get().base();
		const std::uintptr_t table = *reinterpret_cast<std::uintptr_t*>(npc);
		AnimFn** slot = reinterpret_cast<AnimFn**>(table + 0x9F * sizeof(void*));
		const std::uintptr_t now = reinterpret_cast<std::uintptr_t>(*slot);
		bool chained = false;
		if (table - base == 0x25683B0 && now - base != 0xC67750) {   // another plugin hooked it first (weapon-mods does)
			MEMORY_BASIC_INFORMATION info{};
			constexpr DWORD kExec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
			chained = VirtualQuery(reinterpret_cast<const void*>(now), &info, sizeof(info)) && info.State == MEM_COMMIT &&
				(info.Protect & kExec) && !(info.Protect & PAGE_GUARD);
		}
		if (table - base != 0x25683B0 || (now - base != 0xC67750 && !chained)) {
			Note("offsets|hook|no", "[offsets] the NPC class table (+%llX) or its animation slot (+%llX) is not as measured on AE "
				"1.11.240: offsets are laid on once a frame only (they may lag the animation)\n",
				(unsigned long long)(table - base), (unsigned long long)(now - base));
			return;
		}
		animate.store(*slot, std::memory_order_release);
		REL::Relocation<std::uintptr_t> vtbl{ table };
		vtbl.write_vfunc(0x9F, &Animated);
		Note("offsets|hook|yes", "[offsets] offsets follow each NPC's animation update (Actor table +%llX, slot 0x9F%s)\n",
			(unsigned long long)(table - base), chained ? ", chained after another plugin's hook" : "");
	}
}

void SampleBoneOffsets()
{
	std::lock_guard<std::mutex> guard(lock);
	for (const auto& [id, which] : matched) {
		if (which < 0 || specs[which].logBones.empty())
			continue;
		auto st = states.find(id);
		if (st == states.end() || st->second.logged >= specs[which].logSamples)
			continue;
		Actor* actor = G::LookupActor(id);
		NiAVObject* root = actor && !G::Deleted(actor) ? G::Root(actor) : nullptr;
		if (!root || root != st->second.root)
			continue;                // not bound to this 3D yet (UpdateBoneOffsets binds it)
		SampleGuarded(actor, &specs[which], &st->second, root);
	}
}

void UpdateBoneOffsets()
{
	std::lock_guard<std::mutex> guard(lock);
	frame++;
	if (specs.empty())
		return;
	bool any = false;
	for (auto& s : specs) {
		Resolve(s);
		any = any || s.npc;
	}
	if (!any)
		return;
	for (std::uint32_t id : LoadedActorIds()) {
		Actor* actor = G::LookupActor(id);
		if (!actor || G::Deleted(actor) || !G::Root(actor))
			continue;
		auto m = matched.find(id);
		if (m == matched.end()) {
			int which = -1;
			if (RE::TESNPC* npc = actor->GetNPC())
				for (size_t i = 0; i < specs.size(); i++)
					if (specs[i].npc && specs[i].npc == npc->formID)
						which = (int)i;
			if (which < 0 && specs.size() > 0) {
				bool allResolved = true;
				for (auto& s : specs)
					allResolved = allResolved && s.npc;
				if (!allResolved)
					continue;            // ask again once every file's NPC is known
			}
			m = matched.emplace(id, which).first;
		}
		if (m->second < 0)
			continue;
		anyMatched.store(true, std::memory_order_relaxed);
		if (!hookTried)
			InstallHook(actor);
		ActorState& st = states[id];
		// the hook laid them on after this or last frame's animation: nothing to add here. Otherwise (no hook on this
		// runtime, or an update that skipped her) the frame pass does it, as before
		if (animate.load(std::memory_order_relaxed) && st.hookFrame + 2 >= frame && st.hookFrame)
			continue;
		if (ApplyTimed(actor, id, specs[m->second], st))
			st.byFrame++;
	}
	if (states.size() > 256)
		states.clear();
}
