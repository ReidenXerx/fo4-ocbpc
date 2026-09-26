// fo4-ocbpc: modified by fo4-anatomy (ReidenXerx), 2026-09-23: prop colliders and the discovery log (anatomy_ocbpc.log, the last four runs kept).
// The original OpenCBP_FO4 / OCBPC code is under the MIT licence (LICENSE); these changes
// are under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.

#include "CollisionHub.h"
#include "Game.h"
#include "log.h"
#include "Utility.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <io.h>
#include <mutex>
#include <set>
#include <shlobj.h>

std::vector<Collision> otherColliders;
PartitionMap partitions;

// fo4-anatomy discovery log (Documents\My Games\Fallout4\F4SE\anatomy_ocbpc.log): OCBPC's own logging is
// compiled out, so this is the one window into what the fork sees. It notes, once each, every node on a
// nearby actor whose name looks genital or like a toy, and every prop that becomes a collider. That is
// how an unknown creature's penis or a toy gets a name we can put in the config.
// The last runs are kept as anatomy_ocbpc.1.log (the run before this one) to .4.log: a tester's run is
// often followed by another launch before anyone reads it, and a log that starts empty every launch
// loses exactly the run that mattered.
// A second game process started while one runs (a launcher firing twice: seen 2026-09-23) must not
// touch the first one's log. The log is opened with delete sharing, so the newcomer's rotation can move
// it aside while it is written (the first process keeps writing into .1). Where the move still fails
// (a log held by an older build, which did not share delete), the newcomer writes anatomy_ocbpc.pid<N>.log
// instead: its "w" once truncated the running game's log and left a hole of NUL bytes where its lines were.
static const int kKeptRuns = 4;

static FILE* AnatomyLog()
{
	static FILE* handle = nullptr;
	static bool tried = false;
	if (!tried)
	{
		tried = true;
		char docs[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs)))
		{
			std::string base = std::string(docs) + "\\My Games\\Fallout4\\F4SE\\anatomy_ocbpc";
			auto numbered = [&](int n) { return n ? base + "." + std::to_string(n) + ".log" : base + ".log"; };
			DeleteFileA(numbered(kKeptRuns).c_str());
			bool current = true;                     // the name anatomy_ocbpc.log is free for this run
			for (int n = kKeptRuns - 1; n >= 0; n--)
				if (!MoveFileExA(numbered(n).c_str(), numbered(n + 1).c_str(), MOVEFILE_REPLACE_EXISTING) && n == 0 &&
						GetLastError() != ERROR_FILE_NOT_FOUND)
					current = false;
			std::string path = current ? numbered(0) : base + ".pid" + std::to_string(GetCurrentProcessId()) + ".log";
			HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				NULL, current ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
			if (file != INVALID_HANDLE_VALUE)
			{
				int fd = _open_osfhandle(reinterpret_cast<intptr_t>(file), _O_WRONLY | _O_TEXT);
				if (fd < 0)
					CloseHandle(file);                   // the descriptor never took it
				else if (!(handle = _fdopen(fd, "w")))
					_close(fd);                          // the descriptor owns the handle now
			}
			if (handle)
			{
				time_t now = time(nullptr);
				char started[32] = "";
				tm local;
				if (localtime_s(&local, &now) == 0)
					strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &local);
				fprintf(handle, "fo4-ocbpc (fo4-anatomy fork of OCBPC 0.3) discovery log, started %s\n", started);
				fflush(handle);
			}
		}
	}
	return handle;
}

static std::set<std::string> anatomyLogged;
static std::mutex anatomyLock;   // Rapport's face messages arrive on a Papyrus thread, the rest on the main one

static const size_t kLogKeys = 4000;   // distinct lines per run; past it, one line says so and no more
static bool anatomyFull = false;       // that line is written

static void AnatomyNote(const std::string& key, const char* fmt, ...)
{
	std::lock_guard<std::mutex> guard(anatomyLock);
	if (anatomyLogged.size() >= kLogKeys) {
		FILE* log = anatomyFull ? nullptr : AnatomyLog();
		anatomyFull = true;
		if (log) {
			fprintf(log, "[log] full: %u distinct lines, nothing more is noted this run\n", (unsigned)kLogKeys);
			fflush(log);
		}
		return;
	}
	if (!anatomyLogged.insert(key).second)
		return;
	FILE* log = AnatomyLog();
	if (!log)
		return;
	va_list args;
	va_start(args, fmt);
	vfprintf(log, fmt, args);
	va_end(args);
	fflush(log);
}

void AnatomyLogLine(const std::string& key, const char* line)
{
	AnatomyNote(key, "%s", line);
}

bool AnatomyLogSeen(const std::string& key)
{
	std::lock_guard<std::mutex> guard(anatomyLock);
	return anatomyFull || anatomyLogged.count(key) != 0;
}

static bool LooksGenital(const char* name)
{
	if (!name)
		return false;
	std::string n(name);
	std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
	static const char* words[] = { "penis", "cock", "dick", "knot", "dildo", "strap", "toy", "tentacle", "baseball", "vibr", "genit", "phallus" };
	for (auto w : words)
		if (n.find(w) != std::string::npos)
			return true;
	return false;
}

static void AnatomyScan(Actor* actor, NiAVObject* obj, const char* parentName, int depth)
{
	if (!obj || depth > 40)
		return;
	const char* name = G::Name(obj);
	if (LooksGenital(name))
	{
		char key[512];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "%08X|%s|%s", actor->formID, parentName ? parentName : "", name);
		AnatomyNote(key, "[node] actor %08X: '%s' under '%s', world bound r %.2f at (%.1f, %.1f, %.1f)\n",
			actor->formID, name, parentName ? parentName : "", obj->worldBound.fRadius,
			obj->worldBound.center.x, obj->worldBound.center.y, obj->worldBound.center.z);
	}
	NiNode* node = G::AsNode(obj);
	if (!node)
		return;
	for (UInt32 k = 0; k < G::ChildCount(node); k++)
		AnatomyScan(actor, G::Child(node, k), name, depth + 1);
}



int a = 1319;
int b = 2083;
int c = 3169;
long hashSize;

//debug variable
int callCount = 0;

// fo4-anatomy props: whatever an animation hangs on one of the [Props] nodes (a toy, a bat) collides along
// its length. Its extent is read off the rendered bound of the biggest thing attached there: a line of
// spheres from the attach node, through the bound's centre, to its far side. Rebuilt every frame with
// the other colliders, so a prop that appears mid-scene collides at once and one that goes stops.
static float PropLength(const NiPoint3& p) {
	return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

static void AddPropColliders(Actor* actor, NiNode* root)
{
	for (auto& name : propNodes)
	{
		BSFixedString fs(name.c_str());
		NiAVObject* attach = root->GetObjectByName(fs);
		NiNode* attachNode = attach ? G::AsNode(attach) : nullptr;
		if (!attachNode)
			continue;
		float bestRadius = 0.0f;
		NiPoint3 bestCentre;
		for (UInt32 k = 0; k < G::ChildCount(attachNode); k++)
		{
			NiAVObject* child = G::Child(attachNode, k);
			if (child && child->worldBound.fRadius > bestRadius)
			{
				bestRadius = child->worldBound.fRadius;
				bestCentre = NiPoint3(child->worldBound.center.x, child->worldBound.center.y, child->worldBound.center.z);
			}
		}
		if (bestRadius < propMinBound)
			continue;
		// the skeleton node whose rotation the collider offsets are written in (UpdateColliderPositions)
		NiAVObject* skeletonObj = attach;
		bool skeletonFound = false;
		while (G::Parent(skeletonObj))
		{
			if (G::NameIs(G::Parent(skeletonObj), "skeleton.nif")) {
				skeletonObj = G::Parent(skeletonObj);
				skeletonFound = true;
				break;
			}
			skeletonObj = G::Parent(skeletonObj);
		}
		if (!skeletonFound)
			continue;
		NiPoint3 base = G::World(attach).pos;
		NiPoint3 toCentre = bestCentre - base;
		float reach = PropLength(toCentre);
		NiPoint3 dir = reach > 0.5f ? toCentre / reach
		                            : G::World(attach).rot.Transpose() * NiPoint3(0.0f, 1.0f, 0.0f);
		float length = reach + bestRadius;
		if (length > propMaxLength)
			length = propMaxLength;
		std::vector<Sphere> spheres;
		for (float t = 0.0f; t <= length - propRadius + 1e-3f; t += propSpacing)
		{
			Sphere s;
			// UpdateColliderPositions puts a sphere at node + skeletonRot^T * offset
			s.offset = G::Local(skeletonObj).rot * (dir * t);
			s.radius = propRadius;
			s.radiuspwr2 = propRadius * propRadius;
			spheres.push_back(s);
		}
		if (spheres.empty())
			continue;
		char key[256];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "prop|%08X|%s|%d", actor->formID, name.c_str(), (int)bestRadius);
		AnatomyNote(key, "[prop] actor %08X: '%s' carries a prop, bound r %.2f, reach %.2f -> %d spheres over %.1f\n",
			actor->formID, name.c_str(), bestRadius, reach, (int)spheres.size(), length);
		Collision prop = Collision::Collision(attach, spheres);
		prop.colliderActor = actor;
		prop.colliderNodeName = name;
		prop.isProp = true;
		otherColliders.emplace_back(prop);
	}
}

void CreateOtherColliders()
{
	/*LARGE_INTEGER startingTime, endingTime, elapsedMicroseconds;
	LARGE_INTEGER frequency;

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&startingTime);
	LOG("CreateOtherColliders() start");*/
	//int otherActorCount = 0;

	NiAVObject* mostInterestingRoot;
	//logger.Info("ActorCount: %d\n", actorEntries.size());
	for (int i = 0; i < actorEntries.size(); i++)
	{
		// loadedState = G::Root(actor);
			if (actorEntries[i].actor && G::Root(actorEntries[i].actor) && G::Root(actorEntries[i].actor))
			{
				mostInterestingRoot = G::Root(actorEntries[i].actor);
			}
			else
				continue;

		TESObjectREFR* actorRef = actorEntries[i].actor;

		//auto npcWeight = CALL_MEMBER_FN(actorRef, GetWeight)();

		std::vector<ConfigLine>* ColliderNodesListPtr;

		const char * actorrefname = "";
		std::string actorRace = "";

		if (actorEntries[i].actor->formID == 0x14) //If Player
		{
			actorrefname = "Player";
		}
		else
		{
			actorrefname = G::RefNameC(actorRef);
		}

		if (actorEntries[i].actor->race)
		{
			actorRace = std::string(actorEntries[i].actor->race->GetFullName());

			ColliderNodesListPtr = &ColliderNodesList;
		}
		else
		{
			ColliderNodesListPtr = &ColliderNodesList;
		}

		for(int j=0; j<ColliderNodesListPtr->size(); j++)
		{

			BSFixedString fs = ReturnUsableString(ColliderNodesListPtr->at(j).NodeName);
			NiAVObject* node = mostInterestingRoot->GetObjectByName(fs);

			if (node)
			{
				Collision newCol = Collision::Collision(node, ColliderNodesListPtr->at(j).CollisionSpheres);
				newCol.colliderActor = actorEntries[i].actor;
				newCol.colliderNodeName = ColliderNodesListPtr->at(j).NodeName;
				otherColliders.emplace_back(newCol);
			}
		}

		AddPropColliders(actorEntries[i].actor, G::AsNode(mostInterestingRoot));
	}

	// the discovery scan ([Log] discover=1, off by default): every 2 seconds at most
	static clock_t lastScan = 0;
	clock_t now = clock();
	if (discoverNodes && now - lastScan > 2 * CLOCKS_PER_SEC)
	{
		lastScan = now;
		for (int i = 0; i < actorEntries.size(); i++)
		{
			Actor* actor = actorEntries[i].actor;
			if (actor && G::Root(actor) && G::Root(actor))
				AnatomyScan(actor, G::Root(actor), "", 0);
		}
	}
	//printMessageInt("Actor has others around them: ", otherActorCount);
	//printMessageInt("OtherColliderCount found Total: ", otherColliders.size());
	/*QueryPerformanceCounter(&endingTime);
	elapsedMicroseconds.QuadPart = endingTime.QuadPart - startingTime.QuadPart;
	elapsedMicroseconds.QuadPart *= 1000000000LL;
	elapsedMicroseconds.QuadPart /= frequency.QuadPart;
	LOG("CreateOtherColliders() Update Time = %lld ns\n", elapsedMicroseconds.QuadPart);*/
}

//Unfortunately this doesn't work.
//bool CheckPelvisArmor(Actor* actor)
//{
//	return papyrusActor::GetWornForm(actor, 49) != NULL && papyrusActor::GetWornForm(actor, 52) != NULL && papyrusActor::GetWornForm(actor, 53) != NULL && papyrusActor::GetWornForm(actor, 54) != NULL && papyrusActor::GetWornForm(actor, 56) != NULL && papyrusActor::GetWornForm(actor, 58) != NULL;
//}

void UpdateColliderPositions(std::vector<Collision> &colliderList)
{
	bool skeletonFound = false;
	for (int i = 0; i < colliderList.size(); i++)
	{
		for (int j = 0; j < colliderList[i].collisionSpheres.size(); j++)
		{
			auto skeletonObj = colliderList[i].CollisionObject;
			skeletonFound = false;
			while (G::Parent(skeletonObj))
			{
				if (G::NameIs(G::Parent(skeletonObj), "skeleton.nif")) {
					skeletonObj = G::Parent(skeletonObj);
					skeletonFound = true;
					//logger.Info("Skeleton found!\n");
					break;
				}
				skeletonObj = G::Parent(skeletonObj);
			}
			if (skeletonFound == false) {
				continue;
			}

			colliderList[i].collisionSpheres[j].worldPos = G::World(colliderList[i].CollisionObject).pos
														+ G::Local(skeletonObj).rot.Transpose()
														* colliderList[i].collisionSpheres[j].offset;
		}
	}
}

//
std::vector<long> GetHashIdsFromPos(NiPoint3 pos, float radius, long size)
{
	// adjacencyValue is configurable value that extends the radius length
	float radiusplus = radius + adjacencyValue;

	std::vector<long> hashIdList;
	if (size > 0)
	{
		long hashId = unsigned(floor(pos.x / gridsize)*a + floor(pos.y / gridsize)*b + floor(pos.z / gridsize)*c) % size;
		//logger.Info("hashId=%d\n", hashId);
		// if hashId is good as is then store it
		if (hashId < size && hashId >= 0)
			hashIdList.emplace_back(hashId);

		bool xPlus = false;
		bool xMinus = false;
		bool yPlus = false;
		bool yMinus = false;
		bool zPlus = false;
		bool zMinus = false;

		hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor(pos.y / gridsize)*b + floor(pos.z / gridsize)*c) % size;
		//LOG_INFO("hashId=%d", hashId);
		if (hashId < size && hashId >= 0)
		{
			if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
			{
				xPlus = true;
				hashIdList.emplace_back(hashId);
			}
		}

		hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor(pos.y / gridsize)*b + floor(pos.z / gridsize)*c) % size;
		//LOG_INFO("hashId=%d", hashId);
		if (hashId < size && hashId >= 0)
		{
			if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
			{
				xMinus = true;
				hashIdList.emplace_back(hashId);
			}
		}

		hashId = unsigned(floor((pos.x) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor(pos.z / gridsize)*c) % size;
		//LOG_INFO("hashId=%d", hashId);
		if (hashId < size && hashId >= 0)
		{
			if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
			{
				yPlus = true;
				hashIdList.emplace_back(hashId);
			}
		}

		hashId = unsigned(floor((pos.x) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor(pos.z / gridsize)*c) % size;
		//LOG_INFO("hashId=%d", hashId);
		if (hashId < size && hashId >= 0)
		{
			if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
			{
				yMinus = true;
				hashIdList.emplace_back(hashId);
			}
		}

		hashId = unsigned(floor((pos.x) / gridsize)*a + floor((pos.y) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
		//LOG_INFO("hashId=%d", hashId);
		if (hashId < size && hashId >= 0)
		{
			if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
			{
				zPlus = true;
				hashIdList.emplace_back(hashId);
			}
		}

		hashId = unsigned(floor((pos.x) / gridsize)*a + floor((pos.y) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
		//LOG_INFO("hashId=%d", hashId);
		if (hashId < size && hashId >= 0)
		{
			if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
			{
				zMinus = true;
				hashIdList.emplace_back(hashId);
			}
		}

		if (xPlus && yPlus)
		{
			hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor(pos.z / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}

			if (xPlus && yPlus && zPlus)
			{
				hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
				//LOG_INFO("hashId=%d", hashId);
				if (hashId < size && hashId >= 0)
				{
					if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
					{
						hashIdList.emplace_back(hashId);
					}
				}
			}
			else if (xPlus && yPlus && zMinus)
			{
				hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
				//LOG_INFO("hashId=%d", hashId);
				if (hashId < size && hashId >= 0)
				{
					if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
					{
						hashIdList.emplace_back(hashId);
					}
				}
			}
		}
		else if (xMinus && yPlus)
		{
			hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor(pos.z / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}

			if (xMinus && yPlus && zMinus)
			{
				hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
				//LOG_INFO("hashId=%d", hashId);
				if (hashId < size && hashId >= 0)
				{
					if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
					{
						hashIdList.emplace_back(hashId);
					}
				}
			}
			else if (xMinus && yPlus && zPlus)
			{
				hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
				//LOG_INFO("hashId=%d", hashId);
				if (hashId < size && hashId >= 0)
				{
					if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
					{
						hashIdList.emplace_back(hashId);
					}
				}
			}
		}
		else if (xPlus && yMinus)
		{
			hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor(pos.z / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}

			if (xPlus && yMinus && zMinus)
			{
				hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
				//LOG_INFO("hashId=%d", hashId);
				if (hashId < size && hashId >= 0)
				{
					if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
					{
						hashIdList.emplace_back(hashId);
					}
				}
			}
			else if (xPlus && yMinus && zPlus)
			{
				hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
				//LOG_INFO("hashId=%d", hashId);
				if (hashId < size && hashId >= 0)
				{
					if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
					{
						hashIdList.emplace_back(hashId);
					}
				}
			}
		}
		else if (xMinus && yMinus)
		{
			hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor(pos.z / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}

			if (xMinus && yMinus && zMinus)
			{
				hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
				//LOG_INFO("hashId=%d", hashId);
				if (hashId < size && hashId >= 0)
				{
					if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
					{
						hashIdList.emplace_back(hashId);
					}
				}
			}
			else if (xMinus && yMinus && zPlus)
			{
				hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
				//LOG_INFO("hashId=%d", hashId);
				if (hashId < size && hashId >= 0)
				{
					if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
					{
						hashIdList.emplace_back(hashId);
					}
				}
			}
		}

		if (xPlus && zPlus)
		{
			hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor((pos.y) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}
		}
		else if (xMinus && zPlus)
		{
			hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor((pos.y) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}
		}
		else if (xPlus && zMinus)
		{
			hashId = unsigned(floor((pos.x + radiusplus) / gridsize)*a + floor((pos.y) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}
		}
		else if (xMinus && zMinus)
		{
			hashId = unsigned(floor((pos.x - radiusplus) / gridsize)*a + floor((pos.y) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}
		}

		if (yPlus && zPlus)
		{
			hashId = unsigned(floor((pos.x) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}
		}
		else if (yMinus && zPlus)
		{
			hashId = unsigned(floor((pos.x) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor((pos.z + radiusplus) / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}
		}
		else if (yPlus && zMinus)
		{
			hashId = unsigned(floor((pos.x) / gridsize)*a + floor((pos.y + radiusplus) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}
		}
		else if (yMinus && zMinus)
		{
			hashId = unsigned(floor((pos.x) / gridsize)*a + floor((pos.y - radiusplus) / gridsize)*b + floor((pos.z - radiusplus) / gridsize)*c) % size;
			//LOG_INFO("hashId=%d", hashId);
			if (hashId < size && hashId >= 0)
			{
				if (!(std::find(hashIdList.begin(), hashIdList.end(), hashId) != hashIdList.end()))
				{
					hashIdList.emplace_back(hashId);
				}
			}
		}
	}
	return hashIdList;
}

long GetHashIdFromPos(NiPoint3 pos, long size)
{	
	long hashId = unsigned(floor(pos.x / gridsize)*a + floor(pos.y / gridsize)*b + floor(pos.z / gridsize)*c) % size;
	if (hashId < size && hashId >= 0)
		return hashId;
	else
		return -1;

	/*hashId = unsigned(floor((pos.x+radius) / gridsize)*a + floor(pos.y / gridsize)*b + floor(pos.z / gridsize)*c) % size;
	if (hashId < size && hashId >= 0)
		hashIdList.emplace_back(hashId);

	hashId = unsigned(floor((pos.x - radius) / gridsize)*a + floor(pos.y / gridsize)*b + floor(pos.z / gridsize)*c) % size;
	if (hashId < size && hashId >= 0)
		hashIdList.emplace_back(hashId);*/


}