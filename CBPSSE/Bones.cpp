// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-23: our genital bones, added to the loaded skeleton at run time (ocbp.ini [Bones]).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Bones.h"

#include "CollisionHub.h"
#include "log.h"

#include "f4se/BSGeometry.h"
#include "f4se/GameEvents.h"
#include "f4se/GameForms.h"
#include "f4se/GameRTTI.h"
#include "f4se/BSSkin.h"
#include "f4se/GameTypes.h"
#include "f4se/NiNodes.h"
#include "f4se/NiObjects.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	struct BoneDef
	{
		std::string name;
		std::string parent;
		NiPoint3 local;
	};
	std::vector<BoneDef> table;                      // parents before children (sorted at load)
	std::unordered_set<std::string> ours;           // lowercase names, for the skin scan
	const char* kPelvis = "Pelvis_skin";
	// a skin already pointed at our nodes: one of its entries and the node it must still hold. A
	// re-equipped body is a NEW skin (possibly at a reused address), which fails this check.
	// Skins that need nothing (they do not name ours) are remembered too, with index = none. The
	// bones array pointer and count must match as well, so a new skin at a reused address is seen.
	struct Done { NiNode** entries; UInt32 count; UInt32 index; NiNode* node; };
	const UInt32 kNone = 0xFFFFFFFF;
	std::unordered_map<void*, Done> done;

	std::string Lower(std::string s)
	{
		for (auto& c : s)
			c = (char)tolower((unsigned char)c);
		return s;
	}

	bool IsOurs(const char* name)
	{
		return name && ours.count(Lower(name)) > 0;
	}

	void Note(const std::string& key, const char* fmt, ...)
	{
		char line[512];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
		va_end(args);
		AnatomyLogLine(key, line);
	}

	template <class F>
	void VisitGeometry(NiAVObject* obj, F&& f, int depth = 0)
	{
		if (!obj || depth > 64)
			return;
		if (BSGeometry* geo = obj->GetAsBSGeometry()) {
			f(geo);
			return;
		}
		NiNode* node = obj->GetAsNiNode();
		if (!node)
			return;
		for (UInt16 i = 0; i < node->m_children.m_emptyRunStart; i++)
			VisitGeometry(node->m_children.m_data[i], f, depth + 1);
	}

	// A node's world transform as the engine would compute it on its next update: identity local
	// rotation, so it turns with its parent; the offset reaches the world through the transpose of
	// the parent's stored rotation (the fork's Thing.cpp, and SAF: proven in game).
	void InitWorld(NiAVObject* node, NiAVObject* parent, const NiPoint3& local)
	{
		const NiTransform& p = parent->m_worldTransform;
		node->m_worldTransform.rot = p.rot;
		node->m_worldTransform.scale = p.scale;
		node->m_worldTransform.pos = p.pos + p.rot.Transpose() * (local * p.scale);
	}
}

void LoadBonesConfig(INIReader& reader)
{
	table.clear();
	ours.clear();
	std::vector<BoneDef> pending;
	for (auto& entry : reader.Section("Bones")) {
		std::vector<std::string> parts;
		std::stringstream in(entry.second);
		std::string part;
		while (std::getline(in, part, ','))
			parts.push_back(part);
		if (parts.size() != 4)
			continue;
		BoneDef def;
		def.name = entry.first;
		def.parent = parts[0];
		def.parent.erase(0, def.parent.find_first_not_of(" \t"));
		def.parent.erase(def.parent.find_last_not_of(" \t") + 1);
		def.local = NiPoint3(strtof(parts[1].c_str(), nullptr), strtof(parts[2].c_str(), nullptr),
			strtof(parts[3].c_str(), nullptr));
		pending.push_back(def);
	}
	// INIReader keeps a section's keys sorted, so order parents before children here
	std::unordered_set<std::string> placed{ Lower(kPelvis) };
	for (bool progress = true; progress && !pending.empty();) {
		progress = false;
		for (auto it = pending.begin(); it != pending.end();) {
			if (placed.count(Lower(it->parent))) {
				placed.insert(Lower(it->name));
				ours.insert(Lower(it->name));
				table.push_back(*it);
				it = pending.erase(it);
				progress = true;
			}
			else
				++it;
		}
	}
	for (auto& orphan : pending)
		Note("bones|orphan|" + orphan.name, "[bones] %s: its parent %s is neither Pelvis_skin nor one of ours; "
			"skipped\n", orphan.name.c_str(), orphan.parent.c_str());
	std::string names;
	for (auto& def : table)
		names += (names.empty() ? "" : ", ") + def.name;
	char key[48];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|table|%d", (int)table.size());
	Note(key, "[bones] table: %d node(s) from [Bones]: %s\n", (int)table.size(), names.c_str());
}

bool EnsureAnatomyBones(Actor* actor)
{
	if (table.empty() || !actor || !actor->unkF0 || !actor->unkF0->rootNode)
		return false;
	int created = 0, repointed = 0;
	VisitGeometry(actor->unkF0->rootNode, [&](BSGeometry* geo) {
		BSSkin::Instance* skin = geo->skinInstance;
		if (!skin || !skin->bones.entries || !skin->worldTransforms.entries)
			return;
		UInt32 count = skin->bones.count;
		if (skin->worldTransforms.count < count)
			count = skin->worldTransforms.count;
		auto seen = done.find(skin);
		if (seen != done.end() && seen->second.entries == skin->bones.entries && seen->second.count == count &&
				(seen->second.index == kNone ||
				 (seen->second.index < count && skin->bones.entries[seen->second.index] == seen->second.node)))
			return;
		// the skeleton's own Pelvis_skin, as this skin was bound to it, and whether it names ours
		NiNode* pelvis = nullptr;
		bool namesOurs = false;
		int nulls = 0, oursCount = 0;
		for (UInt32 i = 0; i < count; i++) {
			NiNode* b = skin->bones.entries[i];
			if (!b) {
				nulls++;
				continue;
			}
			const char* name = b->m_name.c_str();
			if (name && _stricmp(name, kPelvis) == 0)
				pelvis = b;
			else if (IsOurs(name)) {
				namesOurs = true;
				oursCount++;
			}
		}
		if (pelvis && (namesOurs || nulls)) {           // a body: say once what its skin looks like
			char key[96];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|skin|%08X|%u|%d|%d", actor->formID, count, oursCount, nulls);
			Note(key, "[bones] %08X: a skin of %u bones with Pelvis_skin; %d of them ours, %d empty entries\n",
				actor->formID, count, oursCount, nulls);
		}
		if (!pelvis || !namesOurs) {
			done[skin] = Done{ skin->bones.entries, count, kNone, nullptr };
			return;
		}
		// find or make each of ours under that Pelvis_skin (a search there never meets the body's
		// own stranded copies, which hang off the actor's root, not off the skeleton)
		std::unordered_map<std::string, NiAVObject*> nodes;
		nodes[Lower(kPelvis)] = pelvis;
		for (auto& def : table) {
			auto p = nodes.find(Lower(def.parent));
			if (p == nodes.end() || !p->second)
				continue;
			BSFixedString wanted(def.name.c_str());
			NiAVObject* found = p->second->GetObjectByName(&wanted);
			if (!found) {
				NiNode* parentNode = p->second->GetAsNiNode();
				if (!parentNode)
					continue;
				NiNode* made = NiNode::Create(0);
				if (!made)
					continue;
				CALL_MEMBER_FN(&made->m_name, Set)(def.name.c_str());
				made->m_localTransform.pos = def.local;
				InitWorld(made, parentNode, def.local);
				parentNode->AttachChild(made, true);
				found = made;
				created++;
			}
			nodes[Lower(def.name)] = found;
		}
		// point the skin's entries for our names at those nodes
		for (UInt32 i = 0; i < count; i++) {
			NiNode* b = skin->bones.entries[i];
			if (!b || !IsOurs(b->m_name.c_str()))
				continue;
			auto n = nodes.find(Lower(b->m_name.c_str()));
			if (n == nodes.end() || !n->second || n->second == b)
				continue;
			NiNode* target = n->second->GetAsNiNode();
			if (!target)
				continue;
			skin->bones.entries[i] = target;
			skin->worldTransforms.entries[i] = &target->m_worldTransform;
			repointed++;
		}
		for (UInt32 i = 0; i < count; i++) {               // remember it by one of our entries
			NiNode* b = skin->bones.entries[i];
			if (b && IsOurs(b->m_name.c_str())) {
				done[skin] = Done{ skin->bones.entries, count, i, b };
				break;
			}
		}
	});
	if (done.size() > 4096)
		done.clear();                                   // stale skins of long-gone actors
	if (created || repointed) {
		char key[64];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|%08X|%d|%d", actor->formID, created, repointed);
		Note(key, "[bones] %08X: created %d of our nodes under Pelvis_skin, pointed %d skin entries at them\n",
			actor->formID, created, repointed);
	}
	return created > 0;
}

// ---- A-44: every loaded actor (see Bones.h)
namespace
{
	std::mutex loadedLock;
	std::vector<UInt32> loadedActors;               // form ids, in the order their 3D loaded
	size_t loadedCursor = 0;

	class LoadedSink : public BSTEventSink<TESObjectLoadedEvent>
	{
	public:
		EventResult ReceiveEvent(TESObjectLoadedEvent* evn, void* dispatcher) override
		{
			if (!evn)
				return kEvent_Continue;
			TESForm* form = LookupFormByID(evn->formId);
			if (!form || form->formType != kFormType_ACHR)
				return kEvent_Continue;              // only actors carry a body
			std::lock_guard<std::mutex> guard(loadedLock);
			auto it = std::find(loadedActors.begin(), loadedActors.end(), evn->formId);
			if (evn->loaded && it == loadedActors.end())
				loadedActors.push_back(evn->formId);
			else if (!evn->loaded && it != loadedActors.end())
				loadedActors.erase(it);
			return kEvent_Continue;
		}
	};
	LoadedSink loadedSink;
	bool watching = false;
}

void WatchLoadedActors()
{
	if (watching)
		return;
	auto dispatcher = GetEventDispatcher<TESObjectLoadedEvent>();
	if (!dispatcher)
		return;
	dispatcher->AddEventSink(&loadedSink);
	watching = true;
	Note("bones|watch", "[bones] watching every actor whose 3D loads (TESObjectLoadedEvent), not only the player's cell\n");
}

void EnsureLoadedActors(int budget)
{
	if (table.empty() || budget <= 0)
		return;
	std::vector<UInt32> batch;
	{
		std::lock_guard<std::mutex> guard(loadedLock);
		if (loadedActors.empty())
			return;
		size_t n = loadedActors.size() < (size_t)budget ? loadedActors.size() : (size_t)budget;
		for (size_t k = 0; k < n; k++) {
			if (loadedCursor >= loadedActors.size())
				loadedCursor = 0;
			batch.push_back(loadedActors[loadedCursor++]);
		}
	}
	for (UInt32 id : batch) {
		Actor* actor = DYNAMIC_CAST(LookupFormByID(id), TESForm, Actor);
		if (!actor || (actor->flags & TESForm::kFlag_IsDeleted) || !actor->unkF0 || !actor->unkF0->rootNode)
			continue;
		EnsureAnatomyBones(actor);
	}
}
