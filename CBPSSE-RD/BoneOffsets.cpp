// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-11-23: per-actor bone translation offsets after the animation.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "BoneOffsets.h"

#include "Bones.h"
#include "CollisionHub.h"
#include "Game.h"

#include <windows.h>

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

	struct Offset
	{
		std::string bone;
		bool scale = false;      // *k: the animated local translation times k; else + d
		float k = 1.0f;
		NiPoint3 d;
	};

	struct Spec
	{
		std::string file, plugin;
		std::uint32_t local = 0;     // the NPC_ base form, local to the plugin
		std::uint32_t npc = 0;       // its runtime form id, once the plugin's forms are there
		int nextTry = 0;
		std::vector<Offset> offsets;
	};

	struct BoneState
	{
		NiAVObject* node = nullptr;
		bool top = false;            // no other offset bone above it: its subtree's world is redone from here
		bool valid = false;
		NiPoint3 base, written;      // the pose's value last frame, and what we wrote over it
	};

	struct ActorState
	{
		NiAVObject* root = nullptr;
		std::vector<BoneState> bones;
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
			} else if (_stricmp(section.c_str(), "Offsets") == 0) {
				Offset o;
				if (ParseOffset(key, value, o))
					s.offsets.push_back(o);
				else
					bad++;
			}
		}
		Note("offsets|file|" + s.file, "[offsets] %s: %s base %06X, %zu bones%s\n", s.file.c_str(), s.plugin.c_str(),
			s.local, s.offsets.size(), bad ? " (some lines unreadable: use Bone=*k or Bone=x,y,z)" : "");
		return !s.plugin.empty() && s.local && !s.offsets.empty();
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
			for (auto& o : st.bones)
				if (bone.top && o.node && o.node != bone.node && Above(o.node, bone.node))
					bone.top = false;
		}
		char key[96];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "offsets|bind|%08X|%d", actor->formID, found);
		Note(key, "[offsets] %08X (%s): %d of %zu bones offset after the animation%s%s\n", actor->formID, s.file.c_str(),
			found, s.offsets.size(), missing.empty() ? "" : "; not in the skeleton:", missing.c_str());
	}

	void Apply(Actor* actor, const Spec& s, ActorState& st)
	{
		NiAVObject* root = G::Root(actor);
		if (!root)
			return;
		if (st.root != root || st.bones.size() != s.offsets.size())
			Bind(actor, s, st, root);   // a new 3D: the nodes are found again
		for (size_t i = 0; i < st.bones.size(); i++) {
			BoneState& bone = st.bones[i];
			if (!bone.node)
				continue;
			const Offset& o = s.offsets[i];
			NiPoint3& p = G::Local(bone.node).pos;
			// a bone the animation keys is rewritten every frame; one it leaves alone still holds what we wrote,
			// and gets it again from the kept pose value instead of piling the offset up frame after frame
			const NiPoint3 base = bone.valid && Same(p, bone.written) ? bone.base : p;
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

void UpdateBoneOffsets()
{
	std::lock_guard<std::mutex> guard(lock);
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
		if (!ApplyGuarded(actor, &specs[m->second], &states[id])) {
			states.erase(id);
			char key[64];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "offsets|fault|%08X", id);
			Note(key, "[offsets] %08X: a fault was caught; skipped this frame (her 3D was changing)\n", id);
		}
	}
	if (states.size() > 256)
		states.clear();
}
