// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-23: our genital bones, added to the loaded skeleton at run time (ocbp.ini [Bones]).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Bones.h"

#include "Game.h"
#include "Hook.h"
#include "CollisionHub.h"
#include "log.h"
#include "ActorUtils.h"


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <new>
#include <sstream>
#include <chrono>
#include <cstdint>
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
		std::string lname;                           // name lowercased once at load
	};
	std::vector<BoneDef> table;                      // parents before children (sorted at load)
	// lowercase name -> its entry in table, built once at load. evillasagna (Discord, 2026-11-13): AnatomyBoneRest
	// scanned the whole table and lowercased every name on each call - every physics bone of every actor, every
	// frame (and the skirt and canal per node): heavy lag in crowded cells. One hash lookup now.
	std::unordered_map<std::string, size_t> tableIndex;
	std::unordered_set<std::string> ours;           // lowercase names, for the skin scan
	// A-69: [BonesMale] the same nodes on a man where his body differs (the anus: BodyTalk's is lower and further
	// back than the women's); lowercase name -> offset under its parent
	std::unordered_map<std::string, NiPoint3> maleLocal;
	const char* kPelvis = "Pelvis_skin";
	// fo4-anatomy (2026-11-22, Ilham's Servitron head): [Bones] may also hang nodes under the skeleton's own HEAD (the
	// mouth panel's AnatSrvMouth_L/_R); the Servitron mouth driver below moves them
	const char* kHead = "HEAD";
	std::unordered_set<UInt32> mouthActors;          // actors whose skeleton got our mouth nodes
	// a skin already pointed at our nodes: one of its entries and the node it must still hold. A
	// re-equipped body is a NEW skin (possibly at a reused address), which fails this check.
	// Skins that need nothing (they do not name ours) are remembered too, with index = none. The
	// bones array pointer and count must match as well, so a new skin at a reused address is seen.
	// nulls: the skin's empty entries when remembered. Bodies keep some for good (the owner's 1.2.35 log: the player's
	// 122-bone skin 4, NPC skins 3-12); a count that changes is the game still binding it, so it is looked at again
	struct Done { NiNode** entries; UInt32 count; UInt32 index; NiNode* node; int nulls; };

	int EmptyEntries(G::SkinInstance* skin, UInt32 count)
	{
		int n = 0;
		for (UInt32 i = 0; i < count; i++)
			n += G::SkinBones(skin).entries[i] ? 0 : 1;
		return n;
	}
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
		char line[1024];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
		va_end(args);
		AnatomyLogLine(key, line);
	}

	// one log line: the shape, what is happening, and its skin's entries in order (NULL where the game left one empty)
	std::string SkinEntries(Actor* actor, BSGeometry* geo, G::SkinInstance* skin, UInt32 count, const char* what)
	{
		char head[256];
		int nulls = 0;
		for (UInt32 i = 0; i < count; i++)
			nulls += G::SkinBones(skin).entries[i] ? 0 : 1;
		_snprintf_s(head, sizeof(head), _TRUNCATE, "[bones] %08X: '%s' (%u bones, %d empty) %s; entries:", actor->formID,
			G::Name(geo) ? G::Name(geo) : "?", count, nulls, what);
		std::string s(head);
		for (UInt32 i = 0; i < count; i++) {
			NiNode* b = G::SkinBones(skin).entries[i];
			const char* n = b ? G::Name(b) : nullptr;
			s += ' ';
			s += b ? (n ? n : "(unnamed)") : "NULL";
		}
		s += '\n';
		return s;
	}

	template <class F>
	void VisitGeometry(NiAVObject* obj, F&& f, int depth = 0)
	{
		if (!obj || depth > 64)
			return;
		if (BSGeometry* geo = G::AsGeometry(obj)) {
			f(geo);
			return;
		}
		NiNode* node = G::AsNode(obj);
		if (!node)
			return;
		for (UInt16 i = 0; i < G::ChildCount(node); i++)
			VisitGeometry(G::Child(node, i), f, depth + 1);
	}

	// A node's world transform as the engine would compute it on its next update: identity local
	// rotation, so it turns with its parent; the offset reaches the world through the transpose of
	// the parent's stored rotation (the fork's Thing.cpp, and SAF: proven in game).
	void InitWorld(NiAVObject* node, NiAVObject* parent, const NiPoint3& local)
	{
		const NiTransform& p = G::World(parent);
		G::World(node).rot = p.rot;
		G::World(node).scale = p.scale;
		G::World(node).pos = p.pos + p.rot.Transpose() * (local * p.scale);
	}
}

// fo4-anatomy (11-23, Ilham's penis add-on): a penis worn by a WOMAN whose skeleton has no penis chain (women load
// Skeletal Adjustments' female skeleton, which has none) rides the worn file's own stranded copies: the "fin". [BonesChain]
// chain=Penis_00|..|Penis_05 makes that chain under her Pelvis_skin, nested, where the worn skin binds it - but ONLY on a
// woman, ONLY when her skeleton (under COM) has no node of the root's name, and ONLY with binds unturned against
// Pelvis_skin. Plain [Bones] rows would have made a second chain on every man and pointed men's penis skins at it.
namespace
{
	std::vector<std::string> bonesChain;              // root first
	std::unordered_set<std::string> bonesChainLower;
	std::unordered_set<NiAVObject*> chainMade;        // nodes this made (held for the session, as ours are)

	bool IsChain(const char* name)
	{
		return name && !bonesChainLower.empty() && bonesChainLower.count(Lower(name)) > 0;
	}
}

static void LoadChainConfig(INIReader& reader)
{
	bonesChain.clear();
	bonesChainLower.clear();
	// default: the aim's chain ([Aim] chain), so an Anatomy ini without the section has it; chain= (empty) turns it off
	std::stringstream in(reader.Get("BonesChain", "chain", reader.Get("Aim", "chain", "")));
	std::string part;
	while (std::getline(in, part, '|')) {
		part.erase(0, part.find_first_not_of(" \t"));
		part.erase(part.find_last_not_of(" \t") + 1);
		if (!part.empty()) {
			bonesChain.push_back(part);
			bonesChainLower.insert(Lower(part));
		}
	}
	if (bonesChain.size() < 2) {
		bonesChain.clear();
		bonesChainLower.clear();
	}
	char key[48];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|chaincfg|%d", (int)bonesChain.size());
	Note(key, "[bones] [BonesChain]: %d node(s) made for a woman's worn penis when her skeleton has none\n",
		(int)bonesChain.size());
}

static bool srvPelvisFallback = true;   // [Servitron] pelvisFallback (10-08 test switch)
static bool srvHumanClothes = false;    // [Servitron] humanClothes: human outfits on Servitrons (OFF: a slot-33 outfit replaces the whole robot skin, 10-08)

void LoadBonesConfig(INIReader& reader)
{
	srvPelvisFallback = reader.GetBoolean("Servitron", "pelvisFallback", true);
	srvHumanClothes = reader.GetBoolean("Servitron", "humanClothes", false);
	table.clear();
	tableIndex.clear();
	ours.clear();
	maleLocal.clear();
	std::vector<BoneDef> pending;
	// Section() throws on a section the file lacks (the engine without Anatomy's ini: no [Bones] anywhere)
	const auto haveBones = reader.Sections().count("Bones") > 0;
	for (auto& entry : haveBones ? reader.Section("Bones") : std::unordered_map<std::string, std::string>()) {
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
	std::unordered_set<std::string> placed{ Lower(kPelvis), Lower(kHead) };
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
		Note("bones|orphan|" + orphan.name, "[bones] %s: its parent %s is neither Pelvis_skin, HEAD nor one of ours; "
			"skipped\n", orphan.name.c_str(), orphan.parent.c_str());
	for (size_t i = 0; i < table.size(); i++) {
		table[i].lname = Lower(table[i].name);
		tableIndex.emplace(table[i].lname, i);       // first wins, as the old scan did
	}
	// Section() throws on a section the file lacks: an older Anatomy ini has no [BonesMale]
	const auto haveMale = reader.Sections().count("BonesMale") > 0;
	for (auto& entry : haveMale ? reader.Section("BonesMale") : std::unordered_map<std::string, std::string>()) {   // name=parent,x,y,z: only the offset is a man's own
		std::vector<std::string> parts;
		std::stringstream in(entry.second);
		std::string part;
		while (std::getline(in, part, ','))
			parts.push_back(part);
		if (parts.size() != 4 || !ours.count(Lower(entry.first)))
			continue;
		maleLocal[Lower(entry.first)] = NiPoint3(strtof(parts[1].c_str(), nullptr), strtof(parts[2].c_str(), nullptr),
			strtof(parts[3].c_str(), nullptr));
	}
	LoadChainConfig(reader);
	std::string names;
	for (auto& def : table)
		names += (names.empty() ? "" : ", ") + def.name;
	char key[48];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|table|%d", (int)table.size());
	Note(key, "[bones] table: %d node(s) from [Bones]: %s\n", (int)table.size(), names.c_str());
	_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|male|%d", (int)maleLocal.size());
	Note(key, "[bones] %d of them rest elsewhere on a man ([BonesMale])\n", (int)maleLocal.size());
}

// where a node of ours rests on this actor: [BonesMale]'s offset on a man when it has one, [Bones]' otherwise
static NiPoint3 RestOf(const BoneDef& def, bool male)
{
	if (male) {
		auto it = maleLocal.find(def.lname);
		if (it != maleLocal.end())
			return it->second;
	}
	return def.local;
}

// fo4-anatomy (11-23, Ilham): a body that binds our bones somewhere else than [Bones] (an artist's own vulva, canal and
// anus, weighted to our names where HER geometry is) rests them there. Our own bodies bind exactly at [Bones] (measured
// on the 1.2.6 CBBE and 3BBB prebuilt bodies: all 51 within 0.000), so they keep no entry and nothing changes for them.
namespace
{
	std::mutex boundLock;
	std::unordered_map<UInt32, std::unordered_map<std::string, NiPoint3>> boundRests;   // actor -> lowercase name -> rest
	std::atomic<int> boundActors{ 0 };                // fast path: nobody has one (every other player)
	constexpr float kBoundGap = 0.05f;                // closer than this to [Bones] is [Bones]

	// BSSkin::BoneData (the classic F4SE SDK's BSSkin.h): Instance +0x40 -> BoneData, whose +0x10 array holds per bone a
	// bounding sphere and the skin-to-bone transform the nif was exported with
	struct BoneTransforms
	{
		float sphere[4];
		NiTransform transform;
	};
	static_assert(sizeof(BoneTransforms) == 0x50);
	constexpr std::size_t kSkinBoneData = 0x40, kBoneDataTransforms = 0x10;

	G::Array<BoneTransforms>* BindArray(G::SkinInstance* skin)
	{
		auto data = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(skin) + kSkinBoneData);
		return data ? reinterpret_cast<G::Array<BoneTransforms>*>(data + kBoneDataTransforms) : nullptr;
	}

	// a skin-to-bone transform a real export wrote: unit-ish scale, orthonormal rows (a misread offset cannot pass that)
	bool Sane(const NiTransform& x)
	{
		if (!(x.scale > 0.5f && x.scale < 2.0f))
			return false;
		for (int a = 0; a < 3; a++)
			for (int b = 0; b < 3; b++) {
				float d = 0.0f;
				for (int c = 0; c < 3; c++)
					d += x.rot.data[a][c] * x.rot.data[b][c];
				if (!std::isfinite(d) || std::fabs(d - (a == b ? 1.0f : 0.0f)) > 1e-3f)
					return false;
			}
		return std::isfinite(x.pos.x) && std::isfinite(x.pos.y) && std::isfinite(x.pos.z);
	}

	// where the bone of a skin-to-bone transform sits in skin space (fo4-anatomy tools/nif.py bone_origin)
	NiPoint3 BindOrigin(const NiTransform& x)
	{
		float p[3];
		for (int i = 0; i < 3; i++)
			p[i] = -(x.rot.data[0][i] * x.pos.x + x.rot.data[1][i] * x.pos.y + x.rot.data[2][i] * x.pos.z) / x.scale;
		return NiPoint3(p[0], p[1], p[2]);
	}

	// a skin-space point in a bone's frame
	NiPoint3 IntoBone(const NiTransform& x, const NiPoint3& p)
	{
		float o[3];
		const float v[3] = { p.x, p.y, p.z }, t[3] = { x.pos.x, x.pos.y, x.pos.z };
		for (int r = 0; r < 3; r++)
			o[r] = x.scale * (x.rot.data[r][0] * v[0] + x.rot.data[r][1] * v[1] + x.rot.data[r][2] * v[2]) + t[r];
		return NiPoint3(o[0], o[1], o[2]);
	}

	// the bone's bind turned against Pelvis_skin's: how far from identity (0 = none)
	float BindTurn(const NiTransform& pelvis, const NiTransform& bone)
	{
		float worst = 0.0f;
		for (int a = 0; a < 3; a++)
			for (int b = 0; b < 3; b++) {
				float d = 0.0f;
				for (int c = 0; c < 3; c++)
					d += bone.rot.data[a][c] * pelvis.rot.data[b][c];
				worst = (std::max)(worst, std::fabs(d - (a == b ? 1.0f : 0.0f)));
			}
		return worst;
	}

	bool BoundRest(UInt32 actor, const std::string& lname, NiPoint3& out)
	{
		if (!actor || boundActors.load(std::memory_order_relaxed) == 0)
			return false;
		std::lock_guard<std::mutex> guard(boundLock);
		auto a = boundRests.find(actor);
		if (a == boundRests.end())
			return false;
		auto b = a->second.find(lname);
		if (b == a->second.end())
			return false;
		out = b->second;
		return true;
	}
}

bool AnatomyBoneRest(const std::string& name, bool male, NiPoint3& rest, UInt32 actor)
{
	const std::string lname = Lower(name);
	auto it = tableIndex.find(lname);
	if (it == tableIndex.end())
		return false;
	if (!BoundRest(actor, lname, rest))
		rest = RestOf(table[it->second], male);
	return true;
}

bool AnatomyBoundRests(UInt32 actor)
{
	if (!actor || boundActors.load(std::memory_order_relaxed) == 0)
		return false;
	std::lock_guard<std::mutex> guard(boundLock);
	auto a = boundRests.find(actor);
	return a != boundRests.end();   // has, or had (a body swapped back to one of ours)
}

// Read where this skin binds each of ours under Pelvis_skin (entry `pelvisAt`) and keep the ones off [Bones] for this
// actor; true when any rest changed (the nodes already made must move there)
static bool ReadBoundRests(Actor* actor, G::SkinInstance* skin, UInt32 count, UInt32 pelvisAt, bool male)
{
	G::Array<BoneTransforms>* binds = BindArray(skin);
	if (!binds || !binds->entries || binds->count != G::SkinBones(skin).count || pelvisAt >= count)
		return false;
	const NiTransform& pelvisBind = binds->entries[pelvisAt].transform;
	if (!Sane(pelvisBind)) {
		G::Once("bones|bind|insane", "[bones] a body skin's bind for Pelvis_skin does not read as one: bound rests off");
		return false;
	}
	// every one of ours this skin binds, in Pelvis_skin's frame
	std::unordered_map<std::string, std::pair<NiPoint3, float>> bound;   // lowercase name -> (where, turn)
	for (UInt32 i = 0; i < count; i++) {
		NiNode* b = G::SkinBones(skin).entries[i];
		const char* name = b ? G::Name(b) : nullptr;
		if (!name || !IsOurs(name))
			continue;
		const NiTransform& bind = binds->entries[i].transform;
		if (!Sane(bind))
			continue;
		bound[Lower(name)] = { IntoBone(pelvisBind, BindOrigin(bind)), BindTurn(pelvisBind, bind) };
	}
	std::unordered_map<std::string, NiPoint3> found;
	std::vector<std::string> atTable;
	float turned = 0.0f;
	for (const BoneDef& def : table) {
		if (_stricmp(def.parent.c_str(), kPelvis) != 0)
			continue;                                 // only the nodes hung on Pelvis_skin itself (a child rides its parent)
		NiPoint3 at(0.0f, 0.0f, 0.0f);
		float turn = 0.0f;
		auto own = bound.find(def.lname);
		if (own != bound.end()) {
			at = own->second.first;
			turn = own->second.second;
		} else {
			// 11-25 (Ilham gen5, General-mods): a body that binds only the children (AnatAnus_F_Stretch, AnatLip_L_Stretch:
			// so does our own CBBE body) left the physics parent at [Bones] while its child's verts sat 2 units off: the
			// anus aim read no shift. The parent rests where its bound children say: each child's bind less its own
			// [Bones] offset under the parent, averaged
			int k = 0;
			for (const BoneDef& c : table) {
				if (_stricmp(c.parent.c_str(), def.name.c_str()) != 0)
					continue;
				auto cb = bound.find(c.lname);
				if (cb == bound.end())
					continue;
				at = at + (cb->second.first - RestOf(c, male));
				turn = (std::max)(turn, cb->second.second);
				k++;
			}
			if (!k)
				continue;
			at = at * (1.0f / k);
		}
		const NiPoint3 d = at - RestOf(def, male);
		if (d.x * d.x + d.y * d.y + d.z * d.z > kBoundGap * kBoundGap) {
			found[def.lname] = at;
			turned = (std::max)(turned, turn);
		} else
			atTable.push_back(def.lname);
	}
	bool changed = false;
	size_t kept = 0;
	{
		std::lock_guard<std::mutex> guard(boundLock);
		if (found.empty() && boundRests.find(actor->formID) == boundRests.end())
			return false;                             // one of our bodies, never anything else on her: no entry at all
		auto& mine = boundRests[actor->formID];
		for (auto& [n, p] : found) {
			auto old = mine.find(n);
			if (old == mine.end()) {
				changed = true;
			} else {
				const NiPoint3 d = old->second - p;
				changed |= d.x * d.x + d.y * d.y + d.z * d.z > 1e-4f;
			}
			mine[n] = p;
		}
		for (auto& n : atTable)
			changed |= mine.erase(n) > 0;
		kept = mine.size();   // an emptied entry stays: her physics rests must come back to [Bones] too
		if (boundRests.size() > 4096)
			boundRests.clear();
		boundActors.store((int)boundRests.size(), std::memory_order_relaxed);
	}
	if (!found.empty()) {
		NiPoint3 vulva;
		const bool v = BoundRest(actor->formID, "anatvulva", vulva);
		char key[96];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|bound|%08X|%zu|%d", actor->formID, found.size(),
			v ? (int)(vulva.x * 10) : 0);
		Note(key, "[bones] %08X: her body binds %zu of our bones away from [Bones] (%zu kept); they rest where it binds "
			"them%s\n", actor->formID, found.size(), kept, turned > 0.02f ?
			"; WARNING: some are bound turned against Pelvis_skin (ours are made unturned: those verts twist)" : "");
		if (v) {
			NiPoint3 table0;
			AnatomyBoneRest("AnatVulva", male, table0);
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|boundvulva|%08X|%d|%d", actor->formID, (int)(vulva.x * 10),
				(int)(vulva.y * 10));
			Note(key, "[bones] %08X: AnatVulva bound at (%.2f, %.2f, %.2f), [Bones] (%.2f, %.2f, %.2f)\n", actor->formID,
				vulva.x, vulva.y, vulva.z, table0.x, table0.y, table0.z);
		}
	}
	return changed;
}

// fo4-anatomy (A-51): true when this node hangs, parent by parent, from the actor's live 3D root. A body being
// swapped (a mod unequipping 30 slots in a loop, the owner's AE crash 2026-09-29) has a skin still bound to nodes that
// are about to go, or already gone; those are skipped this frame and tried again on the next.
static bool Reaches(NiAVObject* node, NiAVObject* root)
{
	for (int hops = 0; node && hops < 256; hops++) {
		if (node == root)
			return true;
		node = G::Parent(node);
	}
	return false;
}

// a node of ours under `parent`, made as A-53 / A-52 make them (zeroed, constructed, held for the session)
static NiNode* MakeNode(const std::string& name, NiNode* parent, const NiPoint3& rest)
{
	void* mem = RE::aligned_alloc(alignof(NiNode), sizeof(NiNode));
	if (!mem)
		return nullptr;
	std::memset(mem, 0, sizeof(NiNode));
	NiNode* made = ::new (mem) NiNode(0);
	made->name = name.c_str();
	G::Local(made).pos = rest;
	InitWorld(made, parent, rest);
	parent->AttachChild(made, true);
	made->IncRefCount();
	return made;
}

// [BonesChain] on one skin that names the chain (see LoadChainConfig): true when the skin now points at our chain.
// `pelvis` is this skin's own Pelvis_skin entry (the skeleton's), `pelvisAt` its slot.
static bool EnsureChain(Actor* actor, G::SkinInstance* skin, UInt32 count, NiNode* pelvis, UInt32 pelvisAt)
{
	const size_t n = bonesChain.size();
	std::vector<UInt32> slot(n, kNone);
	for (UInt32 i = 0; i < count; i++) {
		NiNode* b = G::SkinBones(skin).entries[i];
		const char* name = b ? G::Name(b) : nullptr;
		if (!name)
			continue;
		for (size_t k = 0; k < n; k++)
			if (slot[k] == kNone && _stricmp(name, bonesChain[k].c_str()) == 0)
				slot[k] = i;
	}
	char key[96];
	for (size_t k = 0; k < n; k++)
		if (slot[k] == kNone) {
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|chain|partial|%08X", actor->formID);
			Note(key, "[bones] %08X: a worn skin names part of the penis chain, not %s: no chain made\n", actor->formID,
				bonesChain[k].c_str());
			return false;
		}
	// ours already, from an earlier visit (her other paint's penis): reuse, re-rested below
	BSFixedString rootName(bonesChain[0].c_str());
	NiAVObject* mine = pelvis->GetObjectByName(rootName);
	if (mine && (G::Parent(mine) != pelvis || !chainMade.count(mine)))
		mine = nullptr;
	if (!mine) {
		// the skeleton's own chain (under COM): a unified skeleton, a man's. Left alone, exactly as before
		static const BSFixedString com("COM");
		NiAVObject* c = G::Root(actor)->GetObjectByName(com);
		if (!c || c->GetObjectByName(rootName)) {
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|chain|skel|%08X|%d", actor->formID, c ? 1 : 0);
			Note(key, c ? "[bones] %08X: her skeleton has its own %s: the worn penis uses it\n"
			            : "[bones] %08X: no COM under her 3D: no chain made\n", actor->formID, bonesChain[0].c_str());
			return false;
		}
	}
	G::Array<BoneTransforms>* binds = BindArray(skin);
	if (!binds || !binds->entries || binds->count != G::SkinBones(skin).count)
		return false;
	const NiTransform& pelvisBind = binds->entries[pelvisAt].transform;
	if (!Sane(pelvisBind))
		return false;
	std::vector<NiPoint3> at(n);
	float turned = 0.0f;
	for (size_t k = 0; k < n; k++) {
		const NiTransform& bind = binds->entries[slot[k]].transform;
		if (!Sane(bind))
			return false;
		at[k] = IntoBone(pelvisBind, BindOrigin(bind));
		turned = (std::max)(turned, BindTurn(pelvisBind, bind));
	}
	if (turned > 0.02f) {
		_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|chain|turned|%08X", actor->formID);
		Note(key, "[bones] %08X: her worn penis binds its chain turned against Pelvis_skin (%.3f): our nodes are made "
			"unturned, so no chain is made (it would twist her mesh)\n", actor->formID, turned);
		return false;
	}
	std::vector<NiNode*> nodes(n, nullptr);
	int made = 0;
	NiNode* parent = pelvis;
	for (size_t k = 0; k < n; k++) {
		const NiPoint3 rest = k ? at[k] - at[k - 1] : at[0];
		BSFixedString want(bonesChain[k].c_str());
		NiAVObject* found = k == 0 ? mine : parent->GetObjectByName(want);
		if (found && (G::Parent(found) != parent || !chainMade.count(found)))
			found = nullptr;
		NiNode* node = found ? G::AsNode(found) : nullptr;
		if (node) {
			G::Local(node).pos = rest;                 // her other paint binds it a little elsewhere
			InitWorld(node, parent, rest);
		} else {
			node = MakeNode(bonesChain[k], parent, rest);
			if (!node)
				return false;
			chainMade.insert(node);
			made++;
		}
		nodes[k] = node;
		parent = node;
	}
	for (size_t k = 0; k < n; k++) {
		G::SkinBones(skin).entries[slot[k]] = nodes[k];
		G::SkinWorldTransforms(skin).entries[slot[k]] = &G::World(nodes[k]);
	}
	_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|chain|%08X|%d|%d", actor->formID, made, (int)(at[0].y * 10));
	Note(key, "[bones] %08X: her worn penis: %d of %zu chain nodes made under Pelvis_skin where it binds them (%s at "
		"%.2f, %.2f, %.2f), its skin pointed at them\n", actor->formID, made, n, bonesChain[0].c_str(), at[0].x, at[0].y,
		at[0].z);
	return true;
}

static bool EnsureAnatomyBonesImpl(Actor* actor)
{
	if (table.empty() || !actor || !G::Root(actor) || !G::Root(actor))
		return false;
	int created = 0, repointed = 0;
	const bool male = actorUtils::IsActorMale(actor);
	VisitGeometry(G::Root(actor), [&](BSGeometry* geo) {
		G::Once("bones|geo", "diag: first geometry visited: '{}' on {:08X}", G::Name(geo), actor->formID);
		// Runtime Database: the skin's members are 1.10.163's measurements, proven on this runtime first (Layout.cpp)
		Layout::CheckSkin(geo);
		if (Layout::Skin() != Layout::State::kGood)
			return;
		G::SkinInstance* skin = G::Skin(geo);
		if (!skin || !G::SkinBones(skin).entries || !G::SkinWorldTransforms(skin).entries) {
			// 2026-11-22 (Ilham test1: his head's skin never showed in the Servitron lines below): say which shape of a
			// Servitron is skipped here and why
			if (actorUtils::IsServitron(actor)) {
				char key[160];
				_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|srvskip|%08X|%s", actor->formID, G::Name(geo));
				Note(key, "[bones] %08X Servitron: '%s' skipped: %s\n", actor->formID, G::Name(geo),
					!skin ? "no skin instance" : !G::SkinBones(skin).entries ? "no bones array" : "no world transforms");
			}
			return;
		}
		UInt32 count = G::SkinBones(skin).count;
		if (G::SkinWorldTransforms(skin).count < count)
			count = G::SkinWorldTransforms(skin).count;
		auto seen = done.find(skin);
		if (seen != done.end() && seen->second.entries == G::SkinBones(skin).entries && seen->second.count == count &&
				(seen->second.index == kNone ||
				 (seen->second.index < count && G::SkinBones(skin).entries[seen->second.index] == seen->second.node &&
				  Reaches(seen->second.node, G::Root(actor)))) &&  // A-52: our node still on the live skeleton
				(seen->second.nulls == 0 || EmptyEntries(skin, count) == seen->second.nulls))
			return;
		// the skeleton's own Pelvis_skin, as this skin was bound to it, and whether it names ours
		NiNode* pelvis = nullptr;
		UInt32 pelvisAt = kNone;
		bool namesOurs = false, namesChain = false;
		int nulls = 0, oursCount = 0;
		for (UInt32 i = 0; i < count; i++) {
			NiNode* b = G::SkinBones(skin).entries[i];
			if (!b) {
				nulls++;
				continue;
			}
			const char* name = G::Name(b);
			if (name && _stricmp(name, kPelvis) == 0) {
				pelvis = b;
				pelvisAt = i;
			}
			else if (IsOurs(name)) {
				namesOurs = true;
				oursCount++;
			}
			else if (IsChain(name))
				namesChain = true;
		}
		// [BonesChain]: a woman's worn penis on a skeleton without the chain (never a man's, never a Servitron's)
		const bool chainWanted = namesChain && !male && !actorUtils::IsServitron(actor);
		if (actorUtils::IsServitron(actor)) {           // fo4-anatomy (10-08): a Servitron's rubber abdomen got none of ours
			char key[160];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|srv|%08X|%s|%u|%d|%d", actor->formID, G::Name(geo), count,
				oursCount, nulls);
			const char* first = nullptr;
			for (UInt32 i = 0; i < count && !first; i++)
				if (G::SkinBones(skin).entries[i])
					first = G::Name(G::SkinBones(skin).entries[i]);
			Note(key, "[bones] %08X Servitron: '%s' skin of %u bones, Pelvis_skin %s, %d ours, %d empty, first '%s'\n",
				actor->formID, G::Name(geo), count, pelvis ? "found" : "MISSING", oursCount, nulls, first ? first : "-");
		}
		// 11-25 (Ilham gen5: her body and flesh never wired, no "skin of" line ever, while gen1's identical flesh was):
		// a skin seen while the game was still filling its bones (entries NULL) read as "not ours" (or ours half-filled),
		// was remembered as done, and was never looked at again. 1.2.36 then left EVERY human skin with an empty entry
		// alone until none were left - but bodies keep some for good, so no body with one was ever wired (josefkokosnuss
		// 11-26: genitals stretched to the actor's feet, back to normal on 1.2.35). Now it is wired as before and
		// remembered with its count of empty entries: a count that changes (the game filling it) is looked at again.
		if (nulls && !actorUtils::IsServitron(actor)) {
			char key[160];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|empty|%08X|%s|%d", actor->formID, G::Name(geo), nulls);
			AnatomyLogLine(key, SkinEntries(actor, geo, skin, count, "has empty entries: wired now, looked at again if "
				"their count changes").c_str());
		}
		if (pelvis && (namesOurs || nulls)) {           // a body: say once what its skin looks like
			char key[96];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|skin|%08X|%u|%d|%d", actor->formID, count, oursCount, nulls);
			Note(key, "[bones] %08X: a skin of %u bones with Pelvis_skin; %d of them ours, %d empty entries\n",
				actor->formID, count, oursCount, nulls);
		}
		if (pelvis && !Reaches(pelvis, G::Root(actor)))
			return;                                  // A-51: bound to nodes that are not the live skeleton's
		// A human's skin with its Pelvis_skin entry still empty is one the game has not finished binding (an outfit
		// coming off: zenar45 via Watcher, 2026-10-13, OG 1.10.163, a BodyTalk man undressing as AAF walked an NPC couple
		// in; the log's last line, then the desktop, every time). Pointing its entries at our nodes then raced the game's
		// own binding. Left alone, NOT remembered as done: the next visit finds it bound and goes the normal way
		if (!pelvis && (namesOurs || chainWanted) && !actorUtils::IsServitron(actor)) {
			char key[160];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|binding|%08X|%s", actor->formID, G::Name(geo));
			AnatomyLogLine(key, SkinEntries(actor, geo, skin, count, "names ours before the game has bound its "
				"Pelvis_skin: waiting for it").c_str());
			return;
		}
		if (!pelvis && namesOurs && srvPelvisFallback) {
			// fo4-anatomy (10-08): names ours but its Pelvis_skin entry is empty - a Servitron's robot parts leave every
			// skeleton entry of theirs empty (Pelvis_skin, Belly_skin, ...; ours are the file's own nodes). Remembered as
			// done, the rubber abdomen never got our nodes (the owner's AAF scene: its openings stretched to the floor).
			// So the skeleton's OWN Pelvis_skin: the child of Pelvis under COM (an attached model can carry a copy of a
			// name, never that chain)
			BSFixedString pelvisName("Pelvis"), skinName(kPelvis);
			NiAVObject* bone = G::Root(actor)->GetObjectByName(pelvisName);
			NiAVObject* com = bone ? G::Parent(bone) : nullptr;
			NiAVObject* own = (com && G::Name(com) && _stricmp(G::Name(com), "COM") == 0) ? bone->GetObjectByName(skinName)
			                                                                                 : nullptr;
			if (own && G::Parent(own) == bone)
				pelvis = G::AsNode(own);
			char key[64];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|unbound|%08X|%d", actor->formID, pelvis ? 1 : 0);
			Note(key, pelvis ? "[bones] %08X: a skin names ours with its Pelvis_skin entry empty: the skeleton's own used\n"
			                 : "[bones] %08X: a skin names ours with its Pelvis_skin entry empty, and no Pelvis_skin under "
			                   "Pelvis: looking again\n", actor->formID);
			if (!pelvis)
				return;
		}
		const bool chained = chainWanted && pelvis && pelvisAt != kNone &&
			EnsureChain(actor, skin, count, pelvis, pelvisAt);
		if (chained)
			created++;                                   // say it below (and let the caller know her 3D changed)
		if (!pelvis || !namesOurs) {
			UInt32 at = kNone;
			for (UInt32 i = 0; chained && i < count && at == kNone; i++)
				if (G::SkinBones(skin).entries[i] && IsChain(G::Name(G::SkinBones(skin).entries[i])))
					at = i;                              // remembered by its chain entry, re-checked like ours
			done[skin] = Done{ G::SkinBones(skin).entries, count, at, at == kNone ? nullptr : G::SkinBones(skin).entries[at],
			                   nulls };
			return;
		}
		// 11-23: where this body binds ours, when not at [Bones] (an artist's own genitals); the skin's own Pelvis_skin
		// entry only (the Servitron fallback above has no bind to read against)
		const bool movedRests = pelvisAt != kNone && ReadBoundRests(actor, skin, count, pelvisAt, male);
		// find or make each of ours under that Pelvis_skin (a search there never meets the body's
		// own stranded copies, which hang off the actor's root, not off the skeleton)
		std::unordered_map<std::string, NiAVObject*> nodes;
		nodes[Lower(kPelvis)] = pelvis;
		for (auto& def : table) {
			auto p = nodes.find(Lower(def.parent));
			if (p == nodes.end() && _stricmp(def.parent.c_str(), kHead) == 0) {
				// the skeleton's own HEAD (under COM: a worn head carries a flat copy of the name); only a Servitron's
				// head has a mouth panel, so nobody else gets these nodes (or a per-frame mouth visit)
				if (!actorUtils::IsServitron(actor))
					continue;
				BSFixedString headName(kHead);
				p = nodes.emplace(Lower(kHead), G::SkeletonNode(G::Root(actor), headName)).first;
			}
			if (p == nodes.end() || !p->second)
				continue;
			BSFixedString wanted(def.name.c_str());
			NiAVObject* found = p->second->GetObjectByName(wanted);
			if (!found) {
				NiNode* parentNode = G::AsNode(p->second);
				if (!parentNode)
					continue;
				// A-53: the game's heap, zeroed, then constructed. CommonLibF4RD's NiRefObject() never sets refCount (nor
				// NiAVObject() its worldBound), so a plain `new NiNode(0)` started with whatever the heap held there. The
				// owner's AE diag build (2026-09-29) caught it: counts of 2139095041 and 1075650562 at creation, and one
				// node (AnatAnus_L_Stretch) that came in at 0xFFFFFFFF, reached 0 inside AttachChild's own smart-pointer
				// copy and was deleted on the spot, with the body's skin then pointed at it -- the skinning job crashes
				// in NPC scenes and around sleep/redress. The game's own NiNode constructor leaves the count at 0.
				void* mem = RE::aligned_alloc(alignof(NiNode), sizeof(NiNode));
				if (!mem)
					continue;
				std::memset(mem, 0, sizeof(NiNode));
				NiNode* made = ::new (mem) NiNode(0);   // its children vtable by REL::ID
				made->name = def.name.c_str();
				NiPoint3 rest;
				if (!BoundRest(actor->formID, def.lname, rest))
					rest = RestOf(def, male);
				G::Local(made).pos = rest;
				InitWorld(made, parentNode, rest);
				parentNode->AttachChild(made, true);
				// A-52: our own reference, never given back. The owner's AE crashes (2026-09-29, a mod unequipping and
				// re-equipping 30 slots in a loop): a node of ours was freed while body skins still pointed at it, and
				// the game's skinning job walked into it. (BSSkin::Instance's destructor does not release bones, AE
				// +16D91E0: the game's own equip/unequip bone bookkeeping removed it.) Held, a detached node stays valid
				// memory; a few dozen bytes per node for the session.
				made->IncRefCount();
				found = made;
				created++;
			} else if (movedRests && G::Parent(found) == p->second && _stricmp(def.parent.c_str(), kPelvis) == 0) {
				// made earlier for another body (her Vex body swapped for her Nuke one): to where this one binds it
				NiPoint3 rest;
				if (!BoundRest(actor->formID, def.lname, rest))
					rest = RestOf(def, male);
				G::Local(found).pos = rest;
				InitWorld(found, p->second, rest);
			}
			nodes[Lower(def.name)] = found;
			if (_stricmp(def.parent.c_str(), kHead) == 0)
				mouthActors.insert(actor->formID);
		}
		// point the skin's entries for our names at those nodes
		for (UInt32 i = 0; i < count; i++) {
			NiNode* b = G::SkinBones(skin).entries[i];
			if (!b || !IsOurs(G::Name(b)))
				continue;
			auto n = nodes.find(Lower(G::Name(b)));
			if (n == nodes.end() || !n->second || n->second == b)
				continue;
			NiNode* target = G::AsNode(n->second);
			if (!target)
				continue;
			G::SkinBones(skin).entries[i] = target;
			G::SkinWorldTransforms(skin).entries[i] = &G::World(target);
			repointed++;
		}
		for (UInt32 i = 0; i < count; i++) {               // remember it by one of our entries
			NiNode* b = G::SkinBones(skin).entries[i];
			if (b && IsOurs(G::Name(b))) {
				done[skin] = Done{ G::SkinBones(skin).entries, count, i, b, EmptyEntries(skin, count) };
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

	class LoadedSink : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent& evn,
			RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
		{
			TESForm* form = evn.formID ? TESForm::GetFormByID(evn.formID) : nullptr;
			G::Once("bones|event", "diag: first load event: form {:08X} type {} (kACHR {})", evn.formID,
				form ? (int)form->GetFormType() : -1, (int)RE::ENUM_FORM_ID::kACHR);
			if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kACHR)
				return RE::BSEventNotifyControl::kContinue;   // only actors carry a body
			std::lock_guard<std::mutex> guard(loadedLock);
			auto it = std::find(loadedActors.begin(), loadedActors.end(), evn.formID);
			if (evn.loaded && it == loadedActors.end())
				loadedActors.push_back(evn.formID);
			else if (!evn.loaded && it != loadedActors.end())
				loadedActors.erase(it);
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	LoadedSink loadedSink;

	// An actor whose gear just changed: the game re-binds the body, and until our nodes are pointed at again its skin
	// follows the body file's own stranded copies, which hang off the actor's root at standing height. On a standing
	// actor that is invisible; on a corpse it drew a flat blade up out of the butt crack for a split second
	// (bundesgrenzschutz via Watcher, 2026-10-24: a BodyTalk Anatomy man, a dead raider stripped of his outfit). The
	// round robin reached him only every few frames, so these are visited EVERY frame for a while
	constexpr int kHotFrames = 90;
	std::unordered_map<UInt32, int> hotActors;      // form id -> frames left

	class EquipSink : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent& evn,
			RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			TESObjectREFR* ref = evn.actor.get();
			if (!ref || ref->GetFormType() != RE::ENUM_FORM_ID::kACHR)
				return RE::BSEventNotifyControl::kContinue;
			std::lock_guard<std::mutex> guard(loadedLock);
			hotActors[ref->formID] = kHotFrames;
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	EquipSink equipSink;
	bool watching = false, watchingEquip = false;
}

void WatchLoadedActors()
{
	if (!watchingEquip) {
		if (auto* equip = RE::TESEquipEvent::GetEventSource()) {
			equip->RegisterSink(&equipSink);
			watchingEquip = true;
			Note("bones|watchequip", "[bones] watching gear changes (TESEquipEvent): a re-bound body is visited every frame\n");
		}
	}
	if (watching)
		return;
	auto* source = RE::TESObjectLoadedEvent::GetEventSource();
	if (!source)
		return;
	source->RegisterSink(&loadedSink);
	watching = true;
	Note("bones|watch", "[bones] watching every actor whose 3D loads (TESObjectLoadedEvent), not only the player's cell\n");
}

std::vector<UInt32> LoadedActorIds()
{
	std::lock_guard<std::mutex> guard(loadedLock);
	return loadedActors;
}

// A-45: our nodes under the skeleton's Pelvis_skin back where [Bones] rests them; the farthest was off, or -1
static float RestOurBones(Actor* actor)
{
	BSFixedString pelvisName(kPelvis);
	NiAVObject* pelvis = G::SkeletonNode(G::Root(actor), pelvisName);
	if (!pelvis)
		return -1.0f;
	float worst = -1.0f;
	const bool male = actorUtils::IsActorMale(actor);
	for (auto& def : table) {
		BSFixedString wanted(def.name.c_str());
		NiAVObject* node = pelvis->GetObjectByName(wanted);
		if (!node)
			continue;
		NiPoint3 rest;
		if (!BoundRest(actor->formID, def.lname, rest))
			rest = RestOf(def, male);
		NiPoint3 off = G::Local(node).pos - rest;
		float d = sqrtf(off.x * off.x + off.y * off.y + off.z * off.z);
		if (d > worst)
			worst = d;
		G::Local(node).pos = rest;
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 4; c++)
				G::Local(node).rot.data[r][c] = r == c ? 1.0f : 0.0f;
	}
	return worst;
}

// A-51: a fault while her 3D changes under us skips that actor for this frame, logged once, instead of taking the
// game down (the frame runs inside the game's job system, so another job can be rebuilding a body meanwhile).
static void NoteFault(Actor* actor, const char* what)
{
	char key[80];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|fault|%s|%08X", what, actor ? actor->formID : 0);
	Note(key, "[bones] %08X: a fault in %s was caught; the actor is skipped this frame (her 3D was changing)\n",
		actor ? actor->formID : 0, what);
}

bool EnsureAnatomyBones(Actor* actor)
{
	__try {
		return EnsureAnatomyBonesImpl(actor);
	} __except (1) {
		NoteFault(actor, "EnsureAnatomyBones");
		return false;
	}
}

static float RestOurBonesGuarded(Actor* actor)
{
	__try {
		return RestOurBones(actor);
	} __except (1) {
		NoteFault(actor, "RestOurBones");
		return 0.0f;
	}
}

void EnsureLoadedActors(int budget, const std::vector<UInt32>& simulated)
{
	if (table.empty() || budget <= 0)
		return;
	std::vector<UInt32> batch;
	{
		std::lock_guard<std::mutex> guard(loadedLock);
		for (auto it = hotActors.begin(); it != hotActors.end();) {   // gear just changed: every frame, outside the budget
			batch.push_back(it->first);
			it = --it->second <= 0 ? hotActors.erase(it) : std::next(it);
		}
		size_t n = loadedActors.size() < (size_t)budget ? loadedActors.size() : (size_t)budget;
		for (size_t k = 0; k < n; k++) {
			if (loadedCursor >= loadedActors.size())
				loadedCursor = 0;
			batch.push_back(loadedActors[loadedCursor++]);
		}
	}
	for (UInt32 id : batch) {
		Actor* actor = G::LookupActor(id);
		if (!actor || G::Deleted(actor) || !G::Root(actor) || !G::Root(actor))
			continue;
		EnsureAnatomyBones(actor);
		if (std::find(simulated.begin(), simulated.end(), id) != simulated.end())
			continue;                                // OCBPC moves these bones this frame
		float off = RestOurBonesGuarded(actor);
		if (off > 0.5f) {                            // once per actor and size: the owner's theory, measured
			char key[64];
			_snprintf_s(key, sizeof(key), _TRUNCATE, "bones|rest|%08X|%d", id, (int)off);
			Note(key, "[bones] %08X: not simulated (outside the player's cell), our bones put back at rest; they were "
				"up to %.1f units off\n", id, off);
		}
	}
}

static void DescribeBodyImpl(Actor* actor, BodyView& out)
{
	if (!actor || !G::Root(actor))
		return;
	// the skeleton's COM, above Pelvis_skin: a skin bone that does not hang under it is a loose copy the game bound
	// when the skeleton lacked that bone, and the vertices on it stay nailed in place (the owner, 2026-10-04: a
	// player's Ivy, breasts fixed at one height on every new game; the owner had seen the same with skeletons
	// missing 3BBB's breast bones)
	NiAVObject* com = nullptr;
	{
		BSFixedString pelvisName(kPelvis);
		NiAVObject* p = G::SkeletonNode(G::Root(actor), pelvisName);
		for (int hop = 0; p && hop < 8 && !com; hop++, p = G::Parent(p))
			if (G::Name(p) && _stricmp(G::Name(p), "COM") == 0)
				com = p;
	}
	auto underCom = [&](NiAVObject* n) {
		for (int hop = 0; n && hop < 32; hop++, n = G::Parent(n))
			if (n == com)
				return true;
		return false;
	};
	VisitGeometry(G::Root(actor), [&](BSGeometry* geo) {
		const char* name = G::Name(geo);
		out.shapes.push_back(name ? name : "?");
		Layout::CheckSkin(geo);
		if (Layout::Skin() != Layout::State::kGood)
			return;
		G::SkinInstance* skin = G::Skin(geo);
		if (!skin || !G::SkinBones(skin).entries)
			return;
		for (UInt32 i = 0; i < G::SkinBones(skin).count; i++) {
			NiNode* b = G::SkinBones(skin).entries[i];
			if (b && IsOurs(G::Name(b)))
				out.ours++;
			else if (b && com && G::Name(b) && !underCom(b)) {
				std::string bn = G::Name(b);
				if (std::find(out.loose.begin(), out.loose.end(), bn) == out.loose.end())
					out.loose.push_back(bn);
				{
					const std::string shape = name ? name : "?";
					auto& by = out.looseBy[bn];
					if (std::find(by.begin(), by.end(), shape) == by.end())
						by.push_back(shape);
				}
				// the skeleton has the bone after all: this mesh brought its own copy and the game bound the skin to
				// it (falloutfan2077 via Watcher, 2026-10-10: two raiders' Painspike gear flagged "skeleton lacks
				// Head_skin, Neck_skin..." under the vanilla, ZeX and CBBE skeletons alike, all of which have them)
				NiAVObject* real = com->GetObjectByName(BSFixedString(bn.c_str()));
				if (real && real != b) {
					if (std::find(out.copied.begin(), out.copied.end(), bn) == out.copied.end())
						out.copied.push_back(bn);
					const std::string shape = name ? name : "?";
					if (std::find(out.copiedBy.begin(), out.copiedBy.end(), shape) == out.copiedBy.end())
						out.copiedBy.push_back(shape);
				}
			}
		}
	});
}

static bool DescribeBodyGuarded(Actor* actor, BodyView* out)
{
	__try {
		DescribeBodyImpl(actor, *out);
		return true;
	} __except (1) {
		NoteFault(actor, "DescribeBody");
		return false;
	}
}

bool DescribeBody(Actor* actor, BodyView& out)
{
	return DescribeBodyGuarded(actor, &out);
}

static bool SkinUsesBoneImpl(Actor* actor, const std::vector<std::string>& names)
{
	bool found = false;
	if (!actor || !G::Root(actor))
		return false;
	VisitGeometry(G::Root(actor), [&](BSGeometry* geo) {
		if (found)
			return;
		Layout::CheckSkin(geo);
		if (Layout::Skin() != Layout::State::kGood)
			return;
		G::SkinInstance* skin = G::Skin(geo);
		if (!skin || !G::SkinBones(skin).entries)
			return;
		for (UInt32 i = 0; i < G::SkinBones(skin).count && !found; i++) {
			NiNode* b = G::SkinBones(skin).entries[i];
			const char* bn = b ? G::Name(b) : nullptr;
			if (!bn)
				continue;
			for (const auto& n : names)
				if (_stricmp(bn, n.c_str()) == 0) {
					found = true;
					break;
				}
		}
	});
	return found;
}

static bool SkinUsesBoneGuarded(Actor* actor, const std::vector<std::string>* names, bool* out)
{
	__try {
		*out = SkinUsesBoneImpl(actor, *names);
		return true;
	} __except (1) {
		NoteFault(actor, "SkinUsesBone");
		return false;
	}
}

bool SkinUsesBone(Actor* actor, const std::vector<std::string>& names)
{
	bool out = false;
	return SkinUsesBoneGuarded(actor, &names, &out) ? out : true;
}

// the health check (11-07, Ivy Rev 28, a woman New Game with Ace Male Race): her body bound 114 of our bones, yet no node
// named LBreast_01_skin hung anywhere under her 3D. Where the skin's own bone nodes hang says which skeleton the body
// really rides: the parent chain of the node a skin entry points at, up to its top, and whether that top is her 3D root
static void SkinBoneWhereImpl(Actor* actor, const char* name, std::string& out)
{
	NiAVObject* root = actor ? G::Root(actor) : nullptr;
	if (!root)
		return;
	VisitGeometry(root, [&](BSGeometry* geo) {
		if (!out.empty())
			return;
		Layout::CheckSkin(geo);
		if (Layout::Skin() != Layout::State::kGood)
			return;
		G::SkinInstance* skin = G::Skin(geo);
		if (!skin || !G::SkinBones(skin).entries)
			return;
		for (UInt32 i = 0; i < G::SkinBones(skin).count; i++) {
			NiNode* b = G::SkinBones(skin).entries[i];
			const char* bn = b ? G::Name(b) : nullptr;
			if (!bn || _stricmp(bn, name) != 0)
				continue;
			const char* gn = G::Name(geo);
			out = std::string("skin of ") + (gn ? gn : "?") + " binds " + name + ": " + bn;
			NiAVObject* top = b;
			bool underRoot = b == root;
			int hops = 0;
			for (NiAVObject* q = G::Parent(b); q && hops < 48; q = G::Parent(q), ++hops) {
				const char* qn = G::Name(q);
				out += std::string(" <- ") + (qn && *qn ? qn : "(unnamed)");
				top = q;
				underRoot = underRoot || q == root;
			}
			char tail[160];
			NiNode* topNode = G::AsNode(top);
			_snprintf_s(tail, sizeof(tail), _TRUNCATE, " | top %p (%u children)%s", (void*)top,
				topNode ? G::ChildCount(topNode) : 0u,
				underRoot ? ", inside her 3D" : top == b ? ", hangs nowhere (no parent)" : ", NOT inside her 3D");
			out += tail;
			return;
		}
	});
	if (out.empty())
		out = std::string("no skin binds ") + name;
}

static bool SkinBoneWhereGuarded(Actor* actor, const char* name, std::string* out)
{
	__try {
		SkinBoneWhereImpl(actor, name, *out);
		return true;
	} __except (1) {
		NoteFault(actor, "SkinBoneWhere");
		return false;
	}
}

std::string SkinBoneWhere(Actor* actor, const char* name)
{
	std::string out;
	return SkinBoneWhereGuarded(actor, name, &out) ? out : std::string("(her 3D faulted while it was read)");
}

// fo4-anatomy (the owner, 2026-10-08: "make it universally being able wear 3bbb, even a male servitron"): an outfit's
// armour addons list the races that may wear them, and ServitronRace borrows none, so human clothes never showed on a
// Servitron. Its armour race becomes HumanRace at load (no record of anyone's overridden): every human outfit's addons
// then fit her, while her robot parts still match ServitronRace itself. A race some other mod already gave an armour
// race keeps it.
void ServitronClothes()
{
	if (!srvHumanClothes)
		return;
	auto* data = RE::TESDataHandler::GetSingleton();
	if (!data)
		return;
	auto* servitron = data->LookupForm<RE::TESRace>(0x000F99, "Servitron.esm");
	auto* human = data->LookupForm<RE::TESRace>(0x013746, "Fallout4.esm");
	if (!servitron || !human)
		return;
	if (servitron->armorParentRace) {
		Note("srv|clothes|kept", "[servitron] ServitronRace already wears another race's armour: left as it is\n");
		return;
	}
	servitron->armorParentRace = human;
	Note("srv|clothes", "[servitron] ServitronRace wears HumanRace's outfits ([Servitron] humanClothes=1)\n");
}

// ---- fo4-anatomy (2026-11-22, Dudu via Watcher): a Servitron head's sliding mouth panel (Ilham's head, General-mods)
// The panel halves are skinned to AnatSrvMouth_L / _R, which [Bones] hangs under the skeleton's HEAD (EnsureAnatomyBones
// above). Each frame the panel eases toward open while the robot talks or while a hold is running (AnatomyAim.ServitronMouth,
// called by a script before a laser shot), and back to closed; open = [Bones] rest + [ServitronMouth] L/R (HEAD's local
// axes, full travel). A robot without the head never gets the nodes, so nothing here touches it.
namespace
{
	struct MouthState { float open = 0.0f; std::uint64_t holdUntil = 0; };
	std::mutex mouthLock;
	std::unordered_map<UInt32, MouthState> mouths;
	NiPoint3 mouthTravelL(-0.065f, -0.998f, 2.5f), mouthTravelR(-0.065f, -0.998f, -2.5f);   // placeholder, General-mods 11-22
	float mouthSpeed = 4.0f;                         // of the full travel per second
	std::uint64_t mouthLast = 0;

	std::uint64_t MouthNowMs()
	{
		return (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	NiPoint3 ReadPoint(INIReader& reader, const char* key, const NiPoint3& fallback)
	{
		const std::string v = reader.Get("ServitronMouth", key, "");
		float x, y, z;
		return sscanf_s(v.c_str(), "%f,%f,%f", &x, &y, &z) == 3 ? NiPoint3(x, y, z) : fallback;
	}

	bool TalkingGuarded(Actor* actor)
	{
		__try {
			return actor->IsTalking();
		} __except (1) {
			return false;
		}
	}

	void MoveMouth(Actor* actor, float open, bool male)
	{
		{
			BSFixedString headName(kHead);
			NiAVObject* head = G::SkeletonNode(G::Root(actor), headName);
			if (!head)
				return;
			for (int side = 0; side < 2; side++) {
				const char* name = side ? "AnatSrvMouth_R" : "AnatSrvMouth_L";
				BSFixedString wanted(name);
				NiAVObject* node = head->GetObjectByName(wanted);
				NiPoint3 rest;
				if (!node || !AnatomyBoneRest(name, male, rest))
					continue;
				const NiPoint3& t = side ? mouthTravelR : mouthTravelL;
				G::Local(node).pos = rest + t * open;
				if (open > 0.98f) {   // Ilham test3: only one half moved; once per robot and side, what this one drives
					char key[64];
					_snprintf_s(key, sizeof(key), _TRUNCATE, "srv|mouthopen|%08X|%d", actor->formID, side);
					NiAVObject* parent = G::Parent(node);
					const NiPoint3& w = G::World(node).pos;
					Note(key, "[servitron] %08X: %s open: node %p under '%s', local %.2f,%.2f,%.2f (rest %.2f,%.2f,%.2f), "
						"world %.2f,%.2f,%.2f\n", actor->formID, name, (void*)node,
						parent && G::Name(parent) ? G::Name(parent) : "-", G::Local(node).pos.x, G::Local(node).pos.y,
						G::Local(node).pos.z, rest.x, rest.y, rest.z, w.x, w.y, w.z);
				}
			}
		}
	}

	void MoveMouthGuarded(Actor* actor, float open, bool male)
	{
		__try {
			MoveMouth(actor, open, male);
		} __except (1) {
		}
	}

	// Ilham test1 (2026-11-22): his head's panel sat open at rest and no head skin showed in the [bones] lines, so its
	// entries were never pointed at our nodes. Every 2 s per robot: each skin entry named AnatSrvMouth_* that is not our
	// node under the skeleton's HEAD is pointed at it, and what was found is said once per shape and state.
	std::unordered_map<UInt32, std::uint64_t> mouthChecked;

	void BindMouthSkins(Actor* actor)
	{
		BSFixedString headName(kHead);
		NiAVObject* head = G::SkeletonNode(G::Root(actor), headName);
		if (!head)
			return;
		NiNode* ours[2] = { nullptr, nullptr };
		for (int side = 0; side < 2; side++) {
			BSFixedString wanted(side ? "AnatSrvMouth_R" : "AnatSrvMouth_L");
			NiAVObject* n = head->GetObjectByName(wanted);
			ours[side] = (n && G::Parent(n) == head) ? G::AsNode(n) : nullptr;
		}
		VisitGeometry(G::Root(actor), [&](BSGeometry* geo) {
			G::SkinInstance* skin = G::Skin(geo);
			if (!skin || !G::SkinBones(skin).entries || !G::SkinWorldTransforms(skin).entries)
				return;
			UInt32 count = G::SkinBones(skin).count;
			if (G::SkinWorldTransforms(skin).count < count)
				count = G::SkinWorldTransforms(skin).count;
			int named = 0, already = 0, moved = 0, worldFixed = 0;
			char detail[256] = "";
			for (UInt32 i = 0; i < count; i++) {
				NiNode* b = G::SkinBones(skin).entries[i];
				const char* name = b ? G::Name(b) : nullptr;
				if (!name || _strnicmp(name, "AnatSrvMouth_", 13) != 0)
					continue;
				named++;
				NiNode* target = ours[_stricmp(name, "AnatSrvMouth_R") == 0 ? 1 : 0];
				const bool worldOk = G::SkinWorldTransforms(skin).entries[i] == &G::World(b);
				NiNode* parent = G::Parent(b);
				const size_t used = strlen(detail);
				_snprintf_s(detail + used, sizeof(detail) - used, _TRUNCATE, " [slot %u %s %p under '%s', world %s]", i, name,
					(void*)b, parent && G::Name(parent) ? G::Name(parent) : "-", worldOk ? "its own" : "ANOTHER's");
				if (!target)
					continue;
				if (b == target) {
					already++;
					if (!worldOk) {   // Ilham test3 suspect: the entry names our node but draws from another's world
						G::SkinWorldTransforms(skin).entries[i] = &G::World(target);
						worldFixed++;
					}
					continue;
				}
				G::SkinBones(skin).entries[i] = target;
				G::SkinWorldTransforms(skin).entries[i] = &G::World(target);
				moved++;
			}
			if (named) {
				char key[160];
				_snprintf_s(key, sizeof(key), _TRUNCATE, "srv|mouthskin|%08X|%s|%d|%d|%d|%d", actor->formID, G::Name(geo), named,
					already, moved, worldFixed);
				Note(key, "[servitron] %08X: '%s' names the mouth bones (%d entries): %d already ours, %d pointed at ours now, "
					"%d drew from another node's world (fixed)%s; ours L %p R %p;%s\n", actor->formID, G::Name(geo), named,
					already, moved, worldFixed, ours[0] && ours[1] ? "" : " (our nodes missing)", (void*)ours[0],
					(void*)ours[1], detail);
			}
		});
	}

	// Ilham test2 (2026-11-22): no head shape was ever met under his 3D (neither the [bones] listing nor the re-point
	// saw IlhamHead_*). Once per robot: every geometry reached, and every node whose name says head/ilham/mouth/face with
	// its depth and parent, so the log shows where (or whether) the worn head hangs.
	std::unordered_set<UInt32> mouthProbed;

	void ProbeTree(NiAVObject* obj, int depth, std::string& geos, std::string& heads, int& total)
	{
		if (!obj || depth > 96)
			return;
		const char* name = G::Name(obj);
		std::string low = Lower(name ? name : "");
		if (G::AsGeometry(obj)) {
			total++;
			if (geos.size() < 1500)
				geos += (geos.empty() ? "" : ", ") + std::string(name ? name : "?");
			return;
		}
		if (low.find("head") != std::string::npos || low.find("ilham") != std::string::npos ||
				low.find("mouth") != std::string::npos || low.find("face") != std::string::npos) {
			NiAVObject* parent = G::Parent(obj);
			char line[200];
			_snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s@%d<%s", heads.empty() ? "" : ", ", name ? name : "?", depth,
				parent && G::Name(parent) ? G::Name(parent) : "-");
			if (heads.size() < 1500)
				heads += line;
		}
		NiNode* node = G::AsNode(obj);
		if (!node)
			return;
		for (UInt16 i = 0; i < G::ChildCount(node); i++)
			ProbeTree(G::Child(node, i), depth + 1, geos, heads, total);
	}

	void ProbeMouth(Actor* actor)
	{
		std::string geos, heads;
		int total = 0;
		ProbeTree(G::Root(actor), 0, geos, heads, total);
		char key[64];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "srv|probe|%08X", actor->formID);
		Note(key, "[servitron] %08X probe: %d geometries under his 3D: %s\n", actor->formID, total, geos.c_str());
		_snprintf_s(key, sizeof(key), _TRUNCATE, "srv|probeheads|%08X", actor->formID);
		Note(key, "[servitron] %08X probe: head/ilham/mouth/face nodes (name@depth<parent): %s\n", actor->formID,
			heads.empty() ? "none" : heads.c_str());
	}

	void BindMouthSkinsGuarded(Actor* actor)
	{
		__try {
			BindMouthSkins(actor);
		} __except (1) {
		}
		if (mouthProbed.insert(actor->formID).second) {
			__try {
				ProbeMouth(actor);
			} __except (1) {
			}
		}
	}
}

void LoadServitronMouthConfig(INIReader& reader)
{
	mouthTravelL = ReadPoint(reader, "L", mouthTravelL);
	mouthTravelR = ReadPoint(reader, "R", mouthTravelR);
	mouthSpeed = (float)reader.GetReal("ServitronMouth", "speed", mouthSpeed);
}

void HoldServitronMouth(Actor* actor, float seconds)
{
	if (!actor)
		return;
	std::lock_guard<std::mutex> guard(mouthLock);
	mouths[actor->formID].holdUntil = seconds > 0.0f ? MouthNowMs() + (std::uint64_t)(seconds * 1000.0f) : 0;
	char key[64];
	_snprintf_s(key, sizeof(key), _TRUNCATE, "srv|mouth|hold|%08X", actor->formID);
	Note(key, "[servitron] %08X: mouth held open for %.1f s (AnatomyAim.ServitronMouth)\n", actor->formID, seconds);
}

void UpdateServitronMouths()
{
	const std::uint64_t now = MouthNowMs();
	const float dt = mouthLast ? (float)(now - mouthLast) / 1000.0f : 0.0f;
	mouthLast = now;
	if (mouthActors.empty())
		return;
	std::vector<UInt32> ids(mouthActors.begin(), mouthActors.end());
	for (UInt32 id : ids) {
		Actor* actor = G::LookupActor(id);
		if (!actor || G::Deleted(actor) || !G::Root(actor)) {
			mouthActors.erase(id);
			std::lock_guard<std::mutex> guard(mouthLock);
			mouths.erase(id);
			continue;
		}
		float open;
		{
			std::lock_guard<std::mutex> guard(mouthLock);
			MouthState& m = mouths[id];
			const float target = (m.holdUntil > now || TalkingGuarded(actor)) ? 1.0f : 0.0f;
			const float step = mouthSpeed * (dt > 0.1f ? 0.1f : dt);
			m.open += target > m.open ? (std::min)(step, target - m.open) : -(std::min)(step, m.open - target);
			open = m.open;
		}
		auto& checked = mouthChecked[id];
		if (now - checked > 2000) {
			checked = now;
			BindMouthSkinsGuarded(actor);
		}
		MoveMouthGuarded(actor, open, actorUtils::IsActorMale(actor));
	}
}
