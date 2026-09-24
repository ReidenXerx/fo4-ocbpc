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
#include "ActorUtils.h"
#include "log.h"
#include "Thing.h"
#include "config.h"
#include "PapyrusOCBP.h"
#include "SimObj.h"
#include "Bones.h"
#include "Mouth.h"
#include "Utility.hpp"
#include "f4se/GameRTTI.h"
#include "f4se/GameForms.h"
#include "f4se/GameReferences.h"
#include "f4se/NiTypes.h"
#include "f4se/NiNodes.h"
#include "f4se/BSGeometry.h"
#include "f4se/GameThreads.h"
#include "f4se/PluginAPI.h"
#include "f4se/GameStreams.h"
#include "f4se/GameExtraData.h"

#pragma warning(disable : 4996)

using actorUtils::IsActorMale;
using actorUtils::IsActorTorsoArmorEquipped;
using actorUtils::IsActorTrackable;
using actorUtils::IsActorValid;

extern F4SETaskInterface *g_task;

//void UpdateWorldDataToChild(NiAVObject)
void DumpTransform(NiTransform t) {
    Console_Print("%8.2f %8.2f %8.2f", t.rot.data[0][0], t.rot.data[0][1], t.rot.data[0][2]);
    Console_Print("%8.2f %8.2f %8.2f", t.rot.data[1][0], t.rot.data[1][1], t.rot.data[1][2]);
    Console_Print("%8.2f %8.2f %8.2f", t.rot.data[2][0], t.rot.data[2][1], t.rot.data[2][2]);

    Console_Print("%8.2f %8.2f %8.2f", t.pos.x, t.pos.y, t.pos.z);
    Console_Print("%8.2f", t.scale);
}


bool visitObjects(NiAVObject  *parent, std::function<bool(NiAVObject*, int)> functor, int depth = 0) {
    if (!parent) return false;
    NiNode * node = parent->GetAsNiNode();
    if (node) {
        if (functor(parent, depth))
            return true;

        for (UInt32 i = 0; i < node->m_children.m_emptyRunStart; i++) {
            NiAVObject * object = node->m_children.m_data[i];
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

    //NiNode *node = avObj->GetAsNiNode();
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
TESObjectCELL *curCell = nullptr;


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
    auto player = DYNAMIC_CAST(LookupFormByID(0x14), TESForm, Actor);
    if (!player || !player->unkF0) goto FAILED;

    // If player has no cell then return
    auto cell = player->parentCell;
    if (!cell) goto FAILED;

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
        // Attempt to get cell's objects
        for (int i = 0; i < cell->objectList.count; i++) {
            auto ref = cell->objectList[i];
            if (ref) {
                // Attempt to get actors
                auto actor = DYNAMIC_CAST(ref, TESObjectREFR, Actor);
                if (actor && actor->unkF0) {

                    if (actor->unkF0->rootNode)
                    {
                        //Getting border values;
                        actorPos = actor->unkF0->rootNode->m_worldTransform.pos;

                        if (distanceNoSqrt((*g_player)->unkF0->rootNode->m_worldTransform.pos, actorPos) > actorDistance) {
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
                        //logger.Info("Tracking Actor with form ID %08x in cell %ld, race is %s, gender is %d\n", 
                        //    actor->formID, actor->parentCell->formID,
                        //    actor->race->editorId.c_str(),
                        //    IsActorMale(actor));
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

        otherColliders.clear();

        CreateOtherColliders();

        UpdateColliderPositions(otherColliders);

        UpdateMouths();   // fo4-anatomy: every mouth against every penis chain, after the colliders moved

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
    //	auto bodyAV = player->loadedState->node->GetObjectByName(&cs.data);
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

