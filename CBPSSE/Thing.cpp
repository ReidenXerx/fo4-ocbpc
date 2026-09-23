#include "f4se\NiNodes.h"
#include <time.h>

#include "ActorUtils.h"
#include "log.h"
#include "Thing.h"
#include "Utility.hpp"

constexpr auto DEG_TO_RAD = 3.14159265 / 180;
const char* skeletonNif_boneName = "skeleton.nif";
const char* COM_boneName = "COM";

// TODO Make these logger macros
//#define DEBUG 1
//#define TRANSFORM_DEBUG 1
#define COLLISION_DEBUG 1

// <BoneName <ActorFormID, data>>
//pos_map origLocalPos;
//rot_map origLocalRot;

pos_map Thing::origLocalPos;
rot_map Thing::origLocalRot;

void Thing::ShowPos(NiPoint3& p) {
    logger.Info("%8.4f %8.4f %8.4f\n", p.x, p.y, p.z);
}

void Thing::ShowRot(NiMatrix43& r) {
    logger.Info("%8.4f %8.4f %8.4f %8.4f\n", r.data[0][0], r.data[0][1], r.data[0][2], r.data[0][3]);
    logger.Info("%8.4f %8.4f %8.4f %8.4f\n", r.data[1][0], r.data[1][1], r.data[1][2], r.data[1][3]);
    logger.Info("%8.4f %8.4f %8.4f %8.4f\n", r.data[2][0], r.data[2][1], r.data[2][2], r.data[2][3]);
}

Thing::Thing(Actor* actor, NiAVObject* obj, BSFixedString& name)
    : boneName(name)
    , velocity(NiPoint3(0, 0, 0)) {
    
    // Set initial positions
    oldWorldPos = obj->m_worldTransform.pos;
    time = clock();

	float nodescale = 1.0f;
	//if (actor)
	//{
	//	if (actor->unkF0 && actor->unkF0->rootNode)
	//	{
	//		NiAVObject* obj = actor->unkF0->rootNode->GetObjectByName(&name);
	//		if (obj)
	//		{
	//			nodescale = obj->m_worldTransform.scale;
	//		}
	//	}
	//}	

	thingCollisionSpheres = CreateThingCollisionSpheres(actor, std::string(name.c_str()), nodescale);

	skipFramesCount = collisionSkipFrames;
}

Thing::~Thing() {
}

// TODO: this copies an entire vector...
std::vector<Sphere> Thing::CreateThingCollisionSpheres(Actor * actor, std::string nodeName, float nodescale)
{
	auto actorRef = DYNAMIC_CAST(actor, Actor, TESObjectREFR);

	std::vector<ConfigLine>* affectedNodesListPtr;

	const char * actorrefname = "";

	if (actor->formID == 0x14) //If Player
	{
		actorrefname = "Player";
	}
	else
	{
		actorrefname = CALL_MEMBER_FN(actorRef, GetReferenceName)();
	}

	affectedNodesListPtr = &AffectedNodesList;


	std::vector<Sphere> spheres;

	for (int i = 0; i < affectedNodesListPtr->size(); i++)
	{
		if (affectedNodesListPtr->at(i).NodeName == nodeName)
		{
			spheres = affectedNodesListPtr->at(i).CollisionSpheres;

			for(int j=0; j<spheres.size(); j++)
			{
				spheres[j].offset = spheres[j].offset;

				spheres[j].radius = spheres[j].radius * nodescale;

				spheres[j].radiuspwr2 = spheres[j].radius * spheres[j].radius;
			}
			break;
		}
	}
	return spheres;
}

void Thing::UpdateConfig(configEntry_t & centry) {
    stiffness = centry["stiffness"];
    stiffness2 = centry["stiffness2"];
    damping = centry["damping"];

    maxOffsetX = centry["maxoffsetX"];
    maxOffsetY = centry["maxoffsetY"];
    maxOffsetZ = centry["maxoffsetZ"];

    linearX = centry["linearX"];
    linearY = centry["linearY"];
    linearZ = centry["linearZ"];

    rotationalX = centry["rotationalX"];
    rotationalY = centry["rotationalY"];
    rotationalZ = centry["rotationalZ"];

    rotateLinearX = centry["rotateLinearX"];
    rotateLinearY = centry["rotateLinearY"];
    rotateLinearZ = centry["rotateLinearZ"];

    rotateRotationX = centry["rotateRotationX"];
    rotateRotationY = centry["rotateRotationY"];
    rotateRotationZ = centry["rotateRotationZ"];

    timeTick = centry["timetick"];

    if (centry.find("timeStep") != centry.end())
        timeStep = centry["timeStep"];
    else 
        timeStep = 0.016f;

    gravityBias = centry["gravityBias"];
    gravityCorrection = centry["gravityCorrection"];
    cogOffsetY = centry["cogOffsetY"];
    cogOffsetX = centry["cogOffsetX"];
    cogOffsetZ = centry["cogOffsetZ"];
    if (timeTick <= 1)
        timeTick = 1;

    absRotX = centry["absRotX"] != 0.0;

    // fo4-anatomy stretch groups: an absent key reads 0, which leaves the bone out of any group
    stretchGroup = centry.find("stretchGroup") != centry.end() ? centry["stretchGroup"] : 0.0f;
    stretchKnee = centry.find("stretchKnee") != centry.end() ? centry["stretchKnee"] : 0.0f;
    stretchGain = centry.find("stretchGain") != centry.end() ? centry["stretchGain"] : 0.0f;
    stretchMax = centry.find("stretchMax") != centry.end() ? centry["stretchMax"] : 0.0f;
    stretchAxis = NiPoint3(centry.find("stretchAxisX") != centry.end() ? centry["stretchAxisX"] : 0.0f,
                           centry.find("stretchAxisY") != centry.end() ? centry["stretchAxisY"] : 0.0f,
                           centry.find("stretchAxisZ") != centry.end() ? centry["stretchAxisZ"] : 0.0f);
    float axisLength = std::sqrt(stretchAxis.x * stretchAxis.x + stretchAxis.y * stretchAxis.y + stretchAxis.z * stretchAxis.z);
    stretchAxis = axisLength > 1e-4f ? stretchAxis / axisLength : NiPoint3(0, 0, 0);
}

//static float clamp(float val, float min, float max) {
//    if (val < min) return min;
//    else if (val > max) return max;
//    return val;
//}

void Thing::Reset(Actor *actor) {
    auto loadedState = actor->unkF0;
    if (!loadedState || !loadedState->rootNode) {
        logger.Error("No loaded state for actor %08x\n", actor->formID);
        return;
    }
    auto obj = loadedState->rootNode->GetObjectByName(&boneName);

    if (!obj) {
        logger.Error("Couldn't get name for loaded state for actor %08x\n", actor->formID);
        return;
    }

    obj->m_localTransform.pos = origLocalPos[boneName.c_str()][actor->formID];
    obj->m_localTransform.rot = origLocalRot[boneName.c_str()][actor->formID];
}

// Returns 
template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

NiAVObject* Thing::IsActorValid(Actor* actor) {
    if (!actorUtils::IsActorValid(actor)) {
        logger.Error("No valid actor in Thing::Update\n");
        return NULL;
    }
    auto loadedState = actor->unkF0;
    if (!loadedState || !loadedState->rootNode) {
        logger.Error("No loaded state for actor %08x\n", actor->formID);
        return NULL;
    }
    auto obj = loadedState->rootNode->GetObjectByName(&boneName);

    if (!obj) {
        logger.Error("Couldn't get name for loaded state for actor %08x\n", actor->formID);
        return NULL;
    }

    if (!obj->m_parent) {
        logger.Error("Couldn't get bone %s parent for actor %08x\n", boneName.c_str(), actor->formID);
        return NULL;
    }

    return obj;
}

void Thing::Update(Actor *actor) {

	bool collisionsOn = true;

    /*LARGE_INTEGER startingTime, endingTime, elapsedMicroseconds;
    LARGE_INTEGER frequency;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startingTime);*/

    auto obj = IsActorValid(actor);
    if (!obj) {
        return;
    }

    auto newTime = clock();
    auto deltaT = newTime - time;

    time = newTime;
    if (deltaT > 64) deltaT = 64;
    if (deltaT < 8) deltaT = 8;

	NiMatrix43 objRotation;

    objRotation = obj->m_worldTransform.rot;

	bool IsThereCollision = false;
	NiPoint3 collisionDiff = zeroVector;
	long originalDeltaT = deltaT;
	NiPoint3 collisionVector = zeroVector;

	float varCogOffsetX = cogOffsetX;
    float varCogOffsetY = cogOffsetY;
    float varCogOffsetZ = cogOffsetZ;
	float varGravityCorrection = gravityCorrection;
	float varGravityBias = gravityBias;

#if TRANSFORM_DEBUG
    auto sceneObj = obj;
    while (sceneObj->m_parent && sceneObj->m_name != "skeleton.nif")
    {
        logger.Info(sceneObj->m_name);
        logger.Info("\n---\n");
        logger.Error("Actual m_localTransform.pos: ");
        ShowPos(sceneObj->m_localTransform.pos);
        logger.Error("Actual m_worldTransform.pos: ");
        ShowPos(sceneObj->m_worldTransform.pos);
        logger.Info("---\n");
        //logger.Error("Actual m_localTransform.rot Matrix:\n");
        ShowRot(sceneObj->m_localTransform.rot);
        //logger.Error("Actual m_worldTransform.rot Matrix:\n");
        ShowRot(sceneObj->m_worldTransform.rot);
        logger.Info("---\n");
        //if (sceneObj->m_parent) {
        //	logger.Error("Calculated m_worldTransform.pos: ");
        //	ShowPos((sceneObj->m_parent->m_worldTransform.rot.Transpose() * sceneObj->m_localTransform.pos) + sceneObj->m_parent->m_worldTransform.pos);
        //	logger.Error("Calculated m_worldTransform.rot Matrix:\n");
        //	ShowRot(sceneObj->m_localTransform.rot * sceneObj->m_parent->m_worldTransform.rot);
        //}
        sceneObj = sceneObj->m_parent;
    }
#endif

    // Save the bones' original local values if they already haven't
    auto origLocalPos_iter = origLocalPos.find(boneName.c_str());
    auto origLocalRot_iter = origLocalRot.find(boneName.c_str());

    if (origLocalPos_iter == origLocalPos.end()) {
        logger.Error("for bone %s, actor %08x: \n", boneName.c_str(), actor->formID);
        logger.Error("firstRun pos Set: \n");
        origLocalPos[boneName.c_str()][actor->formID] = obj->m_localTransform.pos;
        ShowPos(obj->m_localTransform.pos);
    }
    else {
        auto actorPosMap = origLocalPos.at(boneName.c_str());
        auto actor_iter = actorPosMap.find(actor->formID);
        if (actor_iter == actorPosMap.end()) {
            logger.Error("for bone %s, actor %08x: \n", boneName.c_str(), actor->formID);
            logger.Error("firstRun pos Set: \n");
            origLocalPos[boneName.c_str()][actor->formID] = obj->m_localTransform.pos;
            ShowPos(obj->m_localTransform.pos);
        }
    }
    if (origLocalRot_iter == origLocalRot.end()) {
        logger.Error("for bone %s, actor %08x: \n", boneName.c_str(), actor->formID);
        logger.Error("firstRun rot Set:\n");
        origLocalRot[boneName.c_str()][actor->formID] = obj->m_localTransform.rot;
        ShowRot(obj->m_localTransform.rot);
    }
    else {
        auto actorRotMap = origLocalRot.at(boneName.c_str());
        auto actor_iter = actorRotMap.find(actor->formID);
        if (actor_iter == actorRotMap.end()) {
            logger.Error("for bone %s, actor %08x: \n", boneName.c_str(), actor->formID);
            logger.Error("firstRun rot Set: \n");
            origLocalRot[boneName.c_str()][actor->formID] = obj->m_localTransform.rot;
            ShowRot(obj->m_localTransform.rot);
        }
    }

    auto skeletonObj = obj;
    NiAVObject* comObj;
    bool skeletonFound = false;
    while (skeletonObj->m_parent)
    {
        if (skeletonObj->m_parent->m_name == BSFixedString(COM_boneName)) {
            comObj = skeletonObj->m_parent;
        }
        else if (skeletonObj->m_parent->m_name == BSFixedString(skeletonNif_boneName)) {
            skeletonObj = skeletonObj->m_parent;
            skeletonFound = true;
            break;
        }
        skeletonObj = skeletonObj->m_parent;
    }
    if (skeletonFound == false) {
        logger.Error("Couldn't find skeleton for actor %08x\n", actor->formID);
        return;
    }


    objRotation = skeletonObj->m_localTransform.rot.Transpose();

	std::vector<long> thingIdList;
	std::vector<long> hashIdList;
    //logger.Info("Collisions on: %d, hashSize: %d\n", collisionsOn, hashSize);
	if (collisionsOn && hashSize>0)
	{
		//logger.Info("Before Collision Stuff Start\n");
		// Collision Stuff Start
		for (int i = 0; i < thingCollisionSpheres.size(); i++)
		{
			thingCollisionSpheres[i].worldPos = obj->m_worldTransform.pos + (objRotation*thingCollisionSpheres[i].offset);
			hashIdList = GetHashIdsFromPos(thingCollisionSpheres[i].worldPos, thingCollisionSpheres[i].radius, hashSize);
			for(int m=0; m<hashIdList.size(); m++)
			{
				if (!(std::find(thingIdList.begin(), thingIdList.end(), hashIdList[m]) != thingIdList.end()))
				{
					thingIdList.emplace_back(hashIdList[m]);
				}
			}
		}

		NiPoint3 lastcollisionVector = zeroVector;

		for (int j = 0; j < thingIdList.size(); j++)
		{
			long id = thingIdList[j];
			if (partitions.find(id) != partitions.end())
			{
                for (int i = 0; i < partitions[id].partitionCollisions.size(); i++)
                {
                    // Skip collision with itself?
                    if (partitions[id].partitionCollisions[i].colliderActor == actor && std::strcmp(partitions[id].partitionCollisions[i].colliderNodeName.c_str(), boneName.c_str()) == 0)
                        continue;
                    // fo4-anatomy: a hand prop pushes only the [Props] targets (not her breasts)
                    if (partitions[id].partitionCollisions[i].isProp && !PropReaches(boneName.c_str()))
                        continue;

                    callCount++;

                    // Check if collision vector has changed
                    if (!CompareNiPoints(lastcollisionVector, collisionVector))
                    {
                        // Build thing's collision spheres for current frame
                        for (auto thingCollisSpheres : thingCollisionSpheres)
                        {
                            thingCollisSpheres.worldPos = obj->m_worldTransform.pos + (objRotation * thingCollisSpheres.offset) + collisionVector;
                        }
                    }
                    lastcollisionVector = collisionVector;

                    bool colliding = false;
                    collisionDiff = partitions[id].partitionCollisions[i].CheckCollision(colliding, thingCollisionSpheres, timeTick, originalDeltaT, maxOffsetX, false);
                    if (colliding) {
                        logger.Info("Collision 1 detected!\n");
                        IsThereCollision = true;
                    }   

                    //velocity = velocity + collisionDiff;
					collisionVector = collisionVector + collisionDiff;
				}
			}
		}
		if (IsThereCollision)
		{
			float timeMultiplier = timeTick / (float)deltaT;

            collisionVector.x *= collisionX / linearX;
            collisionVector.y *= collisionY / linearY;
            collisionVector.z *= collisionZ / linearZ;
			collisionVector *= timeMultiplier;
            collisionVector.x = clamp(collisionVector.x, -maxOffsetX, maxOffsetX);
            collisionVector.y = clamp(collisionVector.y, -maxOffsetY, maxOffsetY);
            collisionVector.z = clamp(collisionVector.z, -maxOffsetZ, maxOffsetZ);
            velocity = collisionVector * timeStep;
        }
		//LOG("After Collision Stuff");
	}

	NiPoint3 newPos = oldWorldPos;
	NiPoint3 posDelta = zeroVector;

//#if DEBUG
    logger.Error("bone %s for actor %08x with parent %s\n", boneName.c_str(), actor->formID, skeletonObj->m_name.c_str());
//    ShowRot(skeletonObj->m_worldTransform.rot);
//    //ShowPos(obj->m_parent->m_worldTransform.rot.Transpose() * obj->m_localTransform.pos);
//#endif
    NiMatrix43 targetRot = skeletonObj->m_localTransform.rot.Transpose();
    NiPoint3 origWorldPos = (obj->m_parent->m_worldTransform.rot.Transpose() * origLocalPos[boneName.c_str()][actor->formID]) +  obj->m_parent->m_worldTransform.pos;

    // Offset to move Center of Mass make rotational motion more significant
    NiPoint3 target = (targetRot * NiPoint3(varCogOffsetX, varCogOffsetY, varCogOffsetZ)) + origWorldPos;

#if DEBUG
    logger.Error("World Position: ");
    ShowPos(obj->m_worldTransform.pos);
    logger.Error("oldWorldPos: ");
    ShowPos(oldWorldPos);
    logger.Error("target: ");
    ShowPos(target);
    //    //logger.Error("Parent World Position difference: ");
//    //ShowPos(obj->m_worldTransform.pos - obj->m_parent->m_worldTransform.pos);
    //logger.Error("Target Rotation * cogOffsetY %8.4f: ", cogOffsetY);
    //ShowPos(targetRot * NiPoint3(cogOffsetX, cogOffsetY, cogOffsetZ));
//    //logger.Error("Target Rotation:\n");
//    //ShowRot(targetRot);
//    logger.Error("cogOffset x Transformation:");
//    ShowPos(targetRot * NiPoint3(cogOffsetX, 0, 0));
//    logger.Error("cogOffset y Transformation:");
//    ShowPos(targetRot * NiPoint3(0, cogOffsetY, 0));
//    logger.Error("cogOffset z Transformation:");
//    ShowPos(targetRot * NiPoint3(0, 0, cogOffsetZ));
#endif

    if (!IsThereCollision)
    {
        // diff is Difference in position between old and new world position
        NiPoint3 diff = target - oldWorldPos;

        // Move up in for gravity correction
        diff += targetRot * NiPoint3(0, 0, varGravityCorrection);

        //diff += collisionVector;
//#if DEBUG
        logger.Error("Diff after gravity correction %f: ", varGravityCorrection);
        ShowPos(diff);
//#endif

        if (fabs(diff.x) > 100 || fabs(diff.y) > 100 || fabs(diff.z) > 100) {
            logger.Error("transform reset\n");
            obj->m_localTransform.pos = origLocalPos[boneName.c_str()][actor->formID];
            oldWorldPos = target;
            velocity = NiPoint3(0, 0, 0);
            time = clock();
            lastLocalDiff = NiPoint3(0, 0, 0);
            return;
        }

        float timeMultiplier = timeTick / (float)deltaT;
        diff *= timeMultiplier;

        // Compute the "Spring" Force
        NiPoint3 diff2(diff.x * diff.x * sgn(diff.x), diff.y * diff.y * sgn(diff.y), diff.z * diff.z * sgn(diff.z));
        NiPoint3 force = (diff * stiffness) + (diff2 * stiffness2) - (targetRot * NiPoint3(0, 0, varGravityBias));

#if DEBUG
        logger.Error("Diff2: ");
        ShowPos(diff2);
        logger.Error("Force with stiffness %f, stiffness2 %f, gravity bias %f: ", stiffness, stiffness2, varGravityBias);
        ShowPos(force);
#endif

        do {
            // Assume mass is 1, so Accelleration is Force, can vary mass by changinf force
            //velocity = (velocity + (force * timeStep)) * (1 - (damping * timeStep));
            velocity = (velocity + (force * timeStep)) - (velocity * (damping * timeStep));

            // New position accounting for time
            posDelta += (velocity * timeStep);
            deltaT -= timeTick;
        } while (deltaT >= timeTick);

        if (collisionsOn && hashSize > 0)
        {
            //LOG("Before Maybe Collision Stuff Start");
            NiPoint3 maybePos = newPos + posDelta;

            bool maybeNot = false;

            //After cbp movement collision detection
            thingIdList.clear();
            for (int i = 0; i < thingCollisionSpheres.size(); i++)
            {
                thingCollisionSpheres[i].worldPos = obj->m_worldTransform.pos + (objRotation * thingCollisionSpheres[i].offset) + posDelta;
                hashIdList = GetHashIdsFromPos(thingCollisionSpheres[i].worldPos, thingCollisionSpheres[i].radius, hashSize);
                for (int m = 0; m < hashIdList.size(); m++)
                {
                    if (!(std::find(thingIdList.begin(), thingIdList.end(), hashIdList[m]) != thingIdList.end()))
                    {
                        thingIdList.emplace_back(hashIdList[m]);
                    }
                }
            }
            //Prevent normal movement to cause collision (This prevents shakes)			
            collisionVector = zeroVector;
            NiPoint3 lastcollisionVector = zeroVector;
            for (int j = 0; j < thingIdList.size(); j++)
            {
                long id = thingIdList[j];
                //LOG_INFO("Thing hashId=%d", id);
                if (partitions.find(id) != partitions.end())
                {
                    for (int i = 0; i < partitions[id].partitionCollisions.size(); i++)
                    {
                        if (partitions[id].partitionCollisions[i].colliderActor == actor && std::strcmp(partitions[id].partitionCollisions[i].colliderNodeName.c_str(), boneName.c_str()) == 0)
                            continue;
                        // fo4-anatomy: a hand prop pushes only the [Props] targets (not her breasts)
                        if (partitions[id].partitionCollisions[i].isProp && !PropReaches(boneName.c_str()))
                            continue;

                        callCount++;

                        if (!CompareNiPoints(lastcollisionVector, collisionVector))
                        {
                            // Build thing's collision spheres for current frame after move changes
                            for (auto thingCollisSpheres : thingCollisionSpheres)
                            {
                                thingCollisSpheres.worldPos = obj->m_worldTransform.pos + (objRotation * thingCollisSpheres.offset) + maybePos + collisionVector;
                            }
                        }
                        lastcollisionVector = collisionVector;

                        bool colliding = false;
                        collisionDiff = partitions[id].partitionCollisions[i].CheckCollision(colliding, thingCollisionSpheres, timeTick, originalDeltaT, maxOffsetX, false);
                        if (colliding)
                        {
                            IsThereCollision = true;
                            logger.Info("Collision 2 detected!\n");

                            maybeNot = true;
                            collisionVector = collisionVector + collisionDiff;
                        }
                    }
                }
            }

            if (!maybeNot) {
                logger.Info("Collision 2 didnt happen!\n");
                newPos = maybePos;
            }
            else
            {
                collisionVector.x *= collisionX / linearX;
                collisionVector.y *= collisionY / linearY;
                collisionVector.z *= collisionZ / linearZ;
                collisionVector *= timeMultiplier;
                collisionVector.x = clamp(collisionVector.x, -maxOffsetX, maxOffsetX);
                collisionVector.y = clamp(collisionVector.y, -maxOffsetY, maxOffsetY);
                collisionVector.z = clamp(collisionVector.z, -maxOffsetZ, maxOffsetZ);
                velocity = collisionVector * timeStep;
                newPos = maybePos + collisionVector;
            }

            //LOG("After Maybe Collision Stuff End");
        }
        else {
            logger.Info("Collision 2 fall through!\n");
            newPos = newPos + posDelta;
        }
    }
	else
	{
        logger.Info("Collision 2 skip from Collision 1!\n");
		newPos = newPos + collisionVector;
		collisionOnLastFrame = true;
	}	
        //oldWorldPos = newPos - target;

#if DEBUG
        logger.Error("posDelta: ");
        ShowPos(posDelta);
        logger.Error("newPos: ");
        ShowPos(newPos);
#endif
        // clamp the difference to stop the breast severely lagging at low framerates
        auto diff = newPos - target;

        oldWorldPos = diff + target;

        diff.x = clamp(diff.x, -maxOffsetX, maxOffsetX);
        diff.y = clamp(diff.y, -maxOffsetY, maxOffsetY);
        diff.z = clamp(diff.z - gravityCorrection, -maxOffsetZ, maxOffsetZ) + gravityCorrection;

        oldWorldPos = target + diff;

#if DEBUG
        logger.Error("diff from newPos: ");
        ShowPos(diff);
        //logger.Error("oldWorldPos: ");
        //ShowPos(oldWorldPos);
#endif

        // move the bones based on the supplied weightings
        // Convert the world translations into local coordinates

        NiMatrix43 invRot;

        if (IsThereCollision) {
            invRot = obj->m_parent->m_worldTransform.rot;
        }
        else {
            NiMatrix43 rotateLinear;
            rotateLinear.SetEulerAngles(rotateLinearX* DEG_TO_RAD,
                                        rotateLinearY* DEG_TO_RAD,
                                        rotateLinearZ* DEG_TO_RAD);
            invRot = rotateLinear * obj->m_parent->m_worldTransform.rot;
        }

        auto localDiff = diff;
        localDiff = skeletonObj->m_localTransform.rot * localDiff;
        localDiff.x *= linearX;
        localDiff.y *= linearY;
        localDiff.z *= linearZ;

        auto rotDiff = localDiff;
        localDiff = skeletonObj->m_localTransform.rot.Transpose() * localDiff;

        localDiff = invRot * localDiff;
        oldWorldPos = diff + target;
#if DEBUG
        logger.Error("invRot x=10 Transformation:");
        ShowPos(invRot * NiPoint3(10, 0, 0));
        logger.Error("invRot y=10 Transformation:");
        ShowPos(invRot * NiPoint3(0, 10, 0));
        logger.Error("invRot z=10 Transformation:");
        ShowPos(invRot * NiPoint3(0, 0, 10));
        logger.Error("oldWorldPos: ");
        ShowPos(oldWorldPos);
        logger.Error("localTransform.pos: ");
        ShowPos(obj->m_localTransform.pos);
        logger.Error("localDiff: ");
        ShowPos(localDiff);
        logger.Error("rotDiff: ");
        ShowPos(rotDiff);
#endif
        // scale positions from config
        NiPoint3 newLocalPos = NiPoint3((localDiff.x) + origLocalPos[boneName.c_str()][actor->formID].x,
                                        (localDiff.y) + origLocalPos[boneName.c_str()][actor->formID].y,
                                        (localDiff.z) + origLocalPos[boneName.c_str()][actor->formID].z
        );
        obj->m_localTransform.pos = newLocalPos;
        lastLocalDiff = localDiff;              // fo4-anatomy stretch groups read it after every bone ran

        if (absRotX) rotDiff.x = fabs(rotDiff.x);

        rotDiff.x *= rotationalX;
        rotDiff.y *= rotationalY;
        rotDiff.z *= rotationalZ;


//#if DEBUG
//        logger.Error("localTransform.pos after: ");
//        ShowPos(obj->m_localTransform.pos);
//        logger.Error("origLocalPos:");
//        ShowPos(origLocalPos[boneName.c_str()][actor->formID]);
//        logger.Error("origLocalRot:");
//        ShowRot(origLocalRot[boneName.c_str()][actor->formID]);
//
//#endif
        // Do rotation.
        NiMatrix43 rotateRotation;
        rotateRotation.SetEulerAngles(rotateRotationX * DEG_TO_RAD,
                                      rotateRotationY * DEG_TO_RAD,
                                      rotateRotationZ * DEG_TO_RAD);

        NiMatrix43 standardRot;

        rotDiff = rotateRotation * rotDiff;
        standardRot.SetEulerAngles(rotDiff.x, rotDiff.y, rotDiff.z);
        obj->m_localTransform.rot = standardRot * origLocalRot[boneName.c_str()][actor->formID];

    //logger.Error("end update()\n");
    /*QueryPerformanceCounter(&endingTime);
    elapsedMicroseconds.QuadPart = endingTime.QuadPart - startingTime.QuadPart;
    elapsedMicroseconds.QuadPart *= 1000000000LL;
    elapsedMicroseconds.QuadPart /= frequency.QuadPart;
    _MESSAGE("Thing.update() Update Time = %lld ns\n", elapsedMicroseconds.QuadPart);*/

}