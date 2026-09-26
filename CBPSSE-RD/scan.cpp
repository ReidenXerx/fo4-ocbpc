// fo4-ocbpc: modified by fo4-anatomy (ReidenXerx), 2026-09-23: the run-time bones and the mouths, each frame.
// The original OpenCBP_FO4 / OCBPC code is under the MIT licence (LICENSE); these changes
// are under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
#define DEBUG

#include <cstdlib>
#include <cstdio>

#include <typeinfo>

#include <memory>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cassert>
#include <atomic>
#include <string>
#include <sstream>
#include <iterator>
#include <algorithm>
#include <atomic>
#include <functional>

#include <unordered_set>
#include <unordered_map>

#include "ActorEntry.h"
#include "Game.h"
#include "ActorUtils.h"
#include "log.h"
#include "Thing.h"
#include "config.h"
#include "PapyrusOCBP.h"
#include "SimObj.h"
#include "Bones.h"
#include "Mouth.h"
#include "Aim.h"
#include "TubeCollide.h"
#include "Utility.hpp"

#pragma warning(disable : 4996)

using actorUtils::IsActorMale;
using actorUtils::IsActorTorsoArmorEquipped;
using actorUtils::IsActorTrackable;
using actorUtils::IsActorValid;


//void UpdateWorldDataToChild(NiAVObject)
void DumpTransform(NiTransform) {}   // the classic build printed to the console; nothing reads it


bool visitObjects(NiAVObject  *parent, std::function<bool(NiAVObject*, int)> functor, int depth = 0) {
    if (!parent) return false;
    NiNode * node = G::AsNode(parent);
    if (node) {
        if (functor(parent, depth))
            return true;

        for (UInt32 i = 0; i < G::ChildCount(node); i++) {
            NiAVObject * object = G::Child(node, i);
            if (object) {
                if (visitObjects(object, functor, depth+1))
                    return true;
            }
        }
    }
    else if (functor(parent, depth))
        return true;

    return false;
}

std::string spaces(int n) {
    auto s = std::string(n , ' ');
    return s;
}

bool printStuff(NiAVObject *avObj, int depth) {
    std::string sss = spaces(depth);
    const char *ss = sss.c_str();
    //logger.info("%savObj Name = %s, RTTI = %s\n", ss, avObj->m_name, avObj->GetRTTI()->name);

    //NiNode *node = G::AsNode(avObj);
    //if (node) {
    //	logger.info("%snode %s, RTTI %s\n", ss, node->m_name, node->GetRTTI()->name);
    //}
    //return false;
}

template<class T>
inline void safe_delete(T*& in) {
    if (in) {
        delete in;
        in = NULL;
    }
}



std::unordered_map<UInt32, SimObj> actors;
RE::TESObjectCELL* curCell = nullptr;


void UpdateActors() {
    //LARGE_INTEGER startingTime, endingTime, elapsedMicroseconds;
    //LARGE_INTEGER frequency;

    //QueryPerformanceFrequency(&frequency);
    //QueryPerformanceCounter(&startingTime);

    // We scan the cell and build the list every time - only look up things by ID once
    // we retain all state by actor ID, in a map - it's cleared on cell change
    actorEntries.clear();


	//if (tuningModeCollision != 0)
	//{
	//	frameCount++;
	//	if (frameCount % (120 * tuningModeCollision) == 0)
	//	{
	//		loadMasterConfig();
	//		loadCollisionConfig();
	//		loadExtraCollisionConfig();

	//		actors.clear();
	//	}
	//	if (frameCount >= 1000000)
	//		frameCount = 0;
	//}

    //logger.error("scan Cell\n");
    NiPoint3 actorPos;

    // If no player then return
    auto player = G::LookupActor(0x14);
    if (!player || !G::Root(player)) return;

    // If player has no cell then return
    auto cell = player->parentCell;
    if (!cell) return;

	callCount = 0;

    float xLow = 9999999.0; 
    float xHigh = -9999999.0;
    float yLow = 9999999.0;
    float yHigh = -9999999.0;
    float zLow = 9999999.0;
    float zHigh = -9999999.0;

    if (cell != curCell) {
        logger.Error("cell change %d\n", cell->formID);
        curCell = cell;
        actors.clear();
        actorEntries.clear();
        RefreshHeldFaces();   // fo4-anatomy: no scan this frame; the held faces are found again by form
    } else {
        // fo4-anatomy (Runtime Database): the actors the game is simulating near the player (ProcessLists, the player
        // first), not the object list of the player's cell: TESObjectCELL has no layout in CommonLibF4RD, and the
        // cell's list left out everyone one exterior cell over (A-44). The distance filter below is unchanged, so
        // the physics still runs on the same radius.
        std::vector<Actor*> candidates;
        candidates.push_back(player);
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            for (const auto& handle : lists->highActorHandles) {
                if (auto actorPtr = handle.get(); actorPtr && actorPtr.get() != player)
                    candidates.push_back(actorPtr.get());
            }
        }
        for (Actor* actor : candidates) {
            {
                if (actor && G::Root(actor)) {

                    if (G::Root(actor))
                    {
                        //Getting border values;
                        actorPos = G::World(G::Root(actor)).pos;

                        if (distanceNoSqrt(G::World(G::Root(player)).pos, actorPos) > actorDistance) {
                            logger.Info("Actor with form ID %08x too far away\n", actor->formID);
                            continue;
                        }

                        if (xLow > actorPos.x)
                            xLow = actorPos.x;
                        if (xHigh < actorPos.x)
                            xHigh = actorPos.x;
                        if (yLow > actorPos.y)
                            yLow = actorPos.y;
                        if (yHigh < actorPos.y)
                            yHigh = actorPos.y;
                        if (zLow > actorPos.z)
                            zLow = actorPos.z;
                        if (zHigh < actorPos.z)
                            zHigh = actorPos.z;
                    }

                    // Find if actors is already being tracked
                    auto soIt = actors.find(actor->formID);
                    if (soIt == actors.end() && IsActorTrackable(actor)) {
                        // Make SimObj and place new element in Things
                        auto obj = SimObj(actor, config);
                        if (IsActorValid(actor)) {
                            actors.emplace(actor->formID, obj);
                            actorEntries.emplace_back(ActorEntry{ actor->formID, actor });
                        }
                    }
                    else if (IsActorValid(actor)) {
                        actorEntries.emplace_back(ActorEntry{ actor->formID, actor });
                    }
                }
            }
        }
    }

    // If valid actorPos?
    if (xLow < 9999999 && yLow < 9999999 && zLow < 9999999 && xHigh > -9999999 && yHigh > -9999999 && zHigh > -9999999)
    {
        xLow -= 100.0;
        yLow -= 100.0;
        zLow -= 100.0;
        xHigh += 100.0;
        yHigh += 100.0;
        zHigh += 100.0;

        //SaveLastColliderPositions();

        UpdateAims();     // fo4-anatomy (A-28): every shaft onto its opening, before the colliders are built from it

        otherColliders.clear();

        CreateOtherColliders();

        UpdateColliderPositions(otherColliders);

        UpdateMouths();   // fo4-anatomy: every mouth against every penis chain, after the colliders moved

        BuildTubes();     // fo4-anatomy: the penis chains as tubes for this frame's collisions ([Tube])

        //LoadLastColliderPositions();

        //Spatial Hashing
        hashSize = floor((xHigh - xLow) / gridsize) * floor((yHigh - yLow) / gridsize) * floor((zHigh - zLow) / gridsize);

        /*if (frameCount % 45 == 0)
        LOG_INFO("Hashsize=%d", hashSize);*/

        partitions.clear();

        //logger.Info("Starting collider hashing\n");

        std::vector<long> ids;
        std::vector<long> hashIdList;
        for (int i = 0; i < otherColliders.size(); i++)
        {
            //LOG_INFO("otherColliders[%d]: %s",i, otherColliders[i].CollisionObject->m_name);

            ids.clear();
            for (int j = 0; j < otherColliders[i].collisionSpheres.size(); j++)
            {
                hashIdList = GetHashIdsFromPos(otherColliders[i].collisionSpheres[j].worldPos, otherColliders[i].collisionSpheres[j].radius, hashSize);

                for (int m = 0; m < hashIdList.size(); m++)
                {
                    // Place all unfound IDs into ids and partitions
                    if (std::find(ids.begin(), ids.end(), hashIdList[m]) != ids.end())
                        continue;
                    else
                    {
                        //LOG_INFO("ids.emplace_back(%d)", hashIdList[m]);
                        ids.emplace_back(hashIdList[m]);
                        partitions[hashIdList[m]].partitionCollisions.emplace_back(otherColliders[i]);
                    }
                }
            }
        }
        //LOG_INFO("End of collider hashing");
        //LOG_INFO("Partitions size=%d", partitions.size());
    }

    //static bool done = false;
    //if (!done && player->loadedState->node) {
    //	visitObjects(player->loadedState->node, printStuff);
    //	BSFixedString cs("UUNP");
    //	auto bodyAV = player->loadedState->node->GetObjectByName(cs.data);
    //	BSTriShape *body = bodyAV->GetAsBSTriShape();
    //	logger.info("GetAsBSTriShape returned  %lld\n", body);
    //	auto geometryData = body->geometryData;
    //	//logger.info("Num verts = %d\n", geometryData->m_usVertices);


    //	done = true;
    //}

    // Reload config
    static int count = 0;
    if (configReloadCount && count++ > configReloadCount) {
        count = 0;
        auto reloadActors = LoadConfig();
        for (auto& a : actorEntries) {
            auto objIterator = actors.find(a.id);
            if (objIterator == actors.end()) {
                //logger.error("Sim Object not found in tracked actors\n");
            }
            else {
                objIterator->second.UpdateConfig(a.actor, boneNames, config);
            }
        }

        // Clear actors
        if (reloadActors) {
            actors.clear();
            actorEntries.clear();
        }
    }

    // fo4-anatomy (A-44, A-45): our bones on every loaded actor, not only this cell's, and at rest on those
    // OCBPC does not move this frame
    {
        std::vector<UInt32> simulated;
        for (auto& a : actorEntries)
            simulated.push_back(a.id);
        EnsureLoadedActors(12, simulated);
    }

    //logger.error("Updating %d entities\n", actorEntries.size());
    for (auto &a : actorEntries) {
        EnsureAnatomyBones(a.actor);   // fo4-anatomy (A-21): our bones exist before OCBPC looks them up by name
        auto objIterator = actors.find(a.id);
        if (objIterator == actors.end()) {
            //logger.error("Sim Object not found in tracked actors\n");
        }
        else {
            auto &simObj = objIterator->second;
            if (simObj.IsBound()) {
                // need better system for update config
                if (IsActorTorsoArmorEquipped(a.actor) && detectArmor) {
//                    logger.Info("torso armor detected on actor %x\n", a.actor->formID);
                    simObj.UpdateConfig(a.actor, boneNames, configArmor);
                }
                else {
                    simObj.UpdateConfig(a.actor, boneNames, config);
                }
                simObj.Update(a.actor);
            }
            else {
                if (IsActorTorsoArmorEquipped(a.actor) && detectArmor) {
  //                  logger.Info("torso armor detected on actor %x\n", a.actor->formID);
                    simObj.Bind(a.actor, boneNames, configArmor);
                }
                else {
                    simObj.Bind(a.actor, boneNames, config);
                }
            }
        }
    }

FAILED:
    return;
    //QueryPerformanceCounter(&endingTime);
    //elapsedMicroseconds.QuadPart = endingTime.QuadPart - startingTime.QuadPart;
    //elapsedMicroseconds.QuadPart *= 1000000000LL;
    //elapsedMicroseconds.QuadPart /= frequency.QuadPart;
    //logger.info("Update Time = %lld ns\n", elapsedMicroseconds.QuadPart);
}

