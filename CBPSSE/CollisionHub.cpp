#include "f4se/GameRTTI.h"
#include "f4se_common/Utilities.h"

#include "CollisionHub.h"
#include "log.h"
#include "Utility.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <set>
#include <shlobj.h>

std::vector<Collision> otherColliders;
PartitionMap partitions;

// fo4-anatomy discovery log (Documents\My Games\Fallout4\F4SE\anatomy_ocbpc.log): OCBPC's own logging is
// compiled out, so this is the one window into what the fork sees. It notes, once each, every node on a
// nearby actor whose name looks genital or like a toy, and every prop that becomes a collider. That is
// how an unknown creature's penis or a toy gets a name we can put in the config.
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
			std::string path = std::string(docs) + "\\My Games\\Fallout4\\F4SE\\anatomy_ocbpc.log";
			handle = fopen(path.c_str(), "w");
			if (handle)
			{
				fprintf(handle, "fo4-ocbpc (fo4-anatomy fork of OCBPC 0.3) discovery log\n");
				fflush(handle);
			}
		}
	}
	return handle;
}

static std::set<std::string> anatomyLogged;

static void AnatomyNote(const std::string& key, const char* fmt, ...)
{
	if (anatomyLogged.size() > 4000 || !anatomyLogged.insert(key).second)
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
	const char* name = obj->m_name.c_str();
	if (LooksGenital(name))
	{
		char key[512];
		_snprintf_s(key, sizeof(key), _TRUNCATE, "%08X|%s|%s", actor->formID, parentName ? parentName : "", name);
		AnatomyNote(key, "[node] actor %08X: '%s' under '%s', world bound r %.2f at (%.1f, %.1f, %.1f)\n",
			actor->formID, name, parentName ? parentName : "", obj->m_worldBound.m_fRadius,
			obj->m_worldBound.m_kCenter.x, obj->m_worldBound.m_kCenter.y, obj->m_worldBound.m_kCenter.z);
	}
	NiNode* node = obj->GetAsNiNode();
	if (!node)
		return;
	for (UInt32 k = 0; k < node->m_children.m_emptyRunStart; k++)
		AnatomyScan(actor, node->m_children.m_data[k], name, depth + 1);
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
		NiAVObject* attach = root->GetObjectByName(&fs);
		NiNode* attachNode = attach ? attach->GetAsNiNode() : nullptr;
		if (!attachNode)
			continue;
		float bestRadius = 0.0f;
		NiPoint3 bestCentre;
		for (UInt32 k = 0; k < attachNode->m_children.m_emptyRunStart; k++)
		{
			NiAVObject* child = attachNode->m_children.m_data[k];
			if (child && child->m_worldBound.m_fRadius > bestRadius)
			{
				bestRadius = child->m_worldBound.m_fRadius;
				bestCentre = child->m_worldBound.m_kCenter;
			}
		}
		if (bestRadius < propMinBound)
			continue;
		// the skeleton node whose rotation the collider offsets are written in (UpdateColliderPositions)
		NiAVObject* skeletonObj = attach;
		bool skeletonFound = false;
		while (skeletonObj->m_parent)
		{
			if (skeletonObj->m_parent->m_name == BSFixedString("skeleton.nif")) {
				skeletonObj = skeletonObj->m_parent;
				skeletonFound = true;
				break;
			}
			skeletonObj = skeletonObj->m_parent;
		}
		if (!skeletonFound)
			continue;
		NiPoint3 base = attach->m_worldTransform.pos;
		NiPoint3 toCentre = bestCentre - base;
		float reach = PropLength(toCentre);
		NiPoint3 dir = reach > 0.5f ? toCentre / reach
		                            : attach->m_worldTransform.rot.Transpose() * NiPoint3(0.0f, 1.0f, 0.0f);
		float length = reach + bestRadius;
		if (length > propMaxLength)
			length = propMaxLength;
		std::vector<Sphere> spheres;
		for (float t = 0.0f; t <= length - propRadius + 1e-3f; t += propSpacing)
		{
			Sphere s;
			// UpdateColliderPositions puts a sphere at node + skeletonRot^T * offset
			s.offset = skeletonObj->m_localTransform.rot * (dir * t);
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

	NiNode* mostInterestingRoot;
	//logger.Info("ActorCount: %d\n", actorEntries.size());
	for (int i = 0; i < actorEntries.size(); i++)
	{
		// loadedState = actor->unkF0;
			if (actorEntries[i].actor && actorEntries[i].actor->unkF0 && actorEntries[i].actor->unkF0->rootNode)
			{
				mostInterestingRoot = actorEntries[i].actor->unkF0->rootNode;
			}
			else
				continue;

		auto actorRef = DYNAMIC_CAST(actorEntries[i].actor, Actor, TESObjectREFR);

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
			actorrefname = CALL_MEMBER_FN(actorRef, GetReferenceName)();
		}

		if (actorEntries[i].actor->race)
		{
			actorRace = std::string(actorEntries[i].actor->race->fullName.name.c_str());

			ColliderNodesListPtr = &ColliderNodesList;
		}
		else
		{
			ColliderNodesListPtr = &ColliderNodesList;
		}

		for(int j=0; j<ColliderNodesListPtr->size(); j++)
		{

			BSFixedString fs = ReturnUsableString(ColliderNodesListPtr->at(j).NodeName);
			NiAVObject* node = mostInterestingRoot->GetObjectByName(&fs);

			if (node)
			{
				Collision newCol = Collision::Collision(node, ColliderNodesListPtr->at(j).CollisionSpheres);
				newCol.colliderActor = actorEntries[i].actor;
				newCol.colliderNodeName = ColliderNodesListPtr->at(j).NodeName;
				otherColliders.emplace_back(newCol);
			}
		}

		AddPropColliders(actorEntries[i].actor, mostInterestingRoot);
	}

	// the discovery scan: every 2 seconds at most, so a whole scene graph walk never costs a frame
	static clock_t lastScan = 0;
	clock_t now = clock();
	if (now - lastScan > 2 * CLOCKS_PER_SEC)
	{
		lastScan = now;
		for (int i = 0; i < actorEntries.size(); i++)
		{
			Actor* actor = actorEntries[i].actor;
			if (actor && actor->unkF0 && actor->unkF0->rootNode)
				AnatomyScan(actor, actor->unkF0->rootNode, "", 0);
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
			while (skeletonObj->m_parent)
			{
				if (skeletonObj->m_parent->m_name == BSFixedString("skeleton.nif")) {
					skeletonObj = skeletonObj->m_parent;
					skeletonFound = true;
					//logger.Info("Skeleton found!\n");
					break;
				}
				skeletonObj = skeletonObj->m_parent;
			}
			if (skeletonFound == false) {
				continue;
			}

			colliderList[i].collisionSpheres[j].worldPos = colliderList[i].CollisionObject->m_worldTransform.pos
														+ skeletonObj->m_localTransform.rot.Transpose()
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