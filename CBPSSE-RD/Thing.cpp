// fo4-ocbpc: modified by fo4-anatomy (ReidenXerx), 2026-09-23: stretch groups; a prop pushes only the [Props] targets.
// The original OpenCBP_FO4 / OCBPC code is under the MIT licence (LICENSE); these changes
// are under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include <time.h>

#include "ActorUtils.h"
#include "Game.h"
#include "log.h"
#include "Thing.h"
#include "TubeCollide.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "Utility.hpp"

constexpr auto DEG_TO_RAD = 3.14159265 / 180;
#include <unordered_set>

// fo4-anatomy: a NaN fails every comparison, so the "> 100" reset below never fires on one, and a single non-finite bone
// can take a whole skinned shape with it (its bound), not only the vertices it carries. A players' report, 2026-10-02:
// invisible bodies on pre-placed corpses, only the head and hands left (no logs yet). A bone that goes non-finite or
// absurdly far goes back to its rest, said once per actor and bone.
static bool FiniteNear(const NiPoint3& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::fabs(p.x) < 1e4f && std::fabs(p.y) < 1e4f && std::fabs(p.z) < 1e4f;
}

static bool Finite(const NiMatrix43& m)
{
    for (int r = 0; r < 3; r++)
        for (int col = 0; col < 3; col++)
            if (!std::isfinite(m.data[r][col]))
                return false;
    return true;
}
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
    oldWorldPos = G::World(obj).pos;
    time = clock();

	float nodescale = 1.0f;
	//if (actor)
	//{
	//	if (G::Root(actor) && G::Root(actor))
	//	{
	//		NiAVObject* obj = G::Root(actor)->GetObjectByName(name);
	//		if (obj)
	//		{
	//			nodescale = G::World(obj).scale;
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
	TESObjectREFR* actorRef = actor;

	std::vector<ConfigLine>* affectedNodesListPtr;

	const char * actorrefname = "";

	if (actor->formID == 0x14) //If Player
	{
		actorrefname = "Player";
	}
	else
	{
		actorrefname = G::RefNameC(actorRef);
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
    auto loadedState = G::Root(actor);
    if (!loadedState) {
        logger.Error("No loaded state for actor %08x\n", actor->formID);
        return;
    }
    auto obj = G::SkeletonNode(loadedState, boneName);

    if (!obj) {
        logger.Error("Couldn't get name for loaded state for actor %08x\n", actor->formID);
        return;
    }

    G::Local(obj).pos = origLocalPos[boneName.c_str()][actor->formID];
    G::Local(obj).rot = origLocalRot[boneName.c_str()][actor->formID];
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
    auto loadedState = G::Root(actor);
    if (!loadedState) {
        logger.Error("No loaded state for actor %08x\n", actor->formID);
        return NULL;
    }
    auto obj = G::SkeletonNode(loadedState, boneName);

    if (!obj) {
        logger.Error("Couldn't get name for loaded state for actor %08x\n", actor->formID);
        return NULL;
    }

    if (!G::Parent(obj)) {
        logger.Error("Couldn't get bone %s parent for actor %08x\n", boneName.c_str(), actor->formID);
        return NULL;
    }

    return obj;
}

void Thing::Update(Actor *actor) {

	bool collisionsOn = true;
	// fo4-anatomy (A-55): a body bone (breasts, butt, belly, thighs) meets a collider as a contact: the spring always
	// runs, then the bone is held on the collider's surface. Our genital bones ("Anat...") keep OCBPC's push, which
	// the lips and the anus were tuned on.
	const bool contact = contactConstraint && std::strncmp(boneName.c_str(), "Anat", 4) != 0;

    /*LARGE_INTEGER startingTime, endingTime, elapsedMicroseconds;
    LARGE_INTEGER frequency;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startingTime);*/

    auto obj = IsActorValid(actor);
    if (!obj) {
        lastLocalDiff = NiPoint3(0, 0, 0);     // fo4-anatomy: a bone that did not run this frame pushes no stretch
        return;
    }

    auto newTime = clock();
    auto deltaT = newTime - time;

    time = newTime;
    if (deltaT > 64) deltaT = 64;
    if (deltaT < 8) deltaT = 8;

	NiMatrix43 objRotation;

    objRotation = G::World(obj).rot;

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
    while (G::Parent(sceneObj) && !G::NameIs(sceneObj, "skeleton.nif"))
    {
        logger.Info(G::Name(sceneObj));
        logger.Info("\n---\n");
        logger.Error("Actual m_localTransform.pos: ");
        ShowPos(G::Local(sceneObj).pos);
        logger.Error("Actual m_worldTransform.pos: ");
        ShowPos(G::World(sceneObj).pos);
        logger.Info("---\n");
        //logger.Error("Actual m_localTransform.rot Matrix:\n");
        ShowRot(G::Local(sceneObj).rot);
        //logger.Error("Actual m_worldTransform.rot Matrix:\n");
        ShowRot(G::World(sceneObj).rot);
        logger.Info("---\n");
        //if (G::Parent(sceneObj)) {
        //	logger.Error("Calculated m_worldTransform.pos: ");
        //	ShowPos((G::World(G::Parent(sceneObj)).rot.Transpose() * G::Local(sceneObj).pos) + G::World(G::Parent(sceneObj)).pos);
        //	logger.Error("Calculated m_worldTransform.rot Matrix:\n");
        //	ShowRot(G::Local(sceneObj).rot * G::World(G::Parent(sceneObj)).rot);
        //}
        sceneObj = G::Parent(sceneObj);
    }
#endif

    // Save the bones' original local values if they already haven't
    auto origLocalPos_iter = origLocalPos.find(boneName.c_str());
    auto origLocalRot_iter = origLocalRot.find(boneName.c_str());

    if (origLocalPos_iter == origLocalPos.end()) {
        logger.Error("for bone %s, actor %08x: \n", boneName.c_str(), actor->formID);
        logger.Error("firstRun pos Set: \n");
        origLocalPos[boneName.c_str()][actor->formID] = G::Local(obj).pos;
        ShowPos(G::Local(obj).pos);
    }
    else {
        auto actorPosMap = origLocalPos.at(boneName.c_str());
        auto actor_iter = actorPosMap.find(actor->formID);
        if (actor_iter == actorPosMap.end()) {
            logger.Error("for bone %s, actor %08x: \n", boneName.c_str(), actor->formID);
            logger.Error("firstRun pos Set: \n");
            origLocalPos[boneName.c_str()][actor->formID] = G::Local(obj).pos;
            ShowPos(G::Local(obj).pos);
        }
    }
    if (origLocalRot_iter == origLocalRot.end()) {
        logger.Error("for bone %s, actor %08x: \n", boneName.c_str(), actor->formID);
        logger.Error("firstRun rot Set:\n");
        origLocalRot[boneName.c_str()][actor->formID] = G::Local(obj).rot;
        ShowRot(G::Local(obj).rot);
    }
    else {
        auto actorRotMap = origLocalRot.at(boneName.c_str());
        auto actor_iter = actorRotMap.find(actor->formID);
        if (actor_iter == actorRotMap.end()) {
            logger.Error("for bone %s, actor %08x: \n", boneName.c_str(), actor->formID);
            logger.Error("firstRun rot Set: \n");
            origLocalRot[boneName.c_str()][actor->formID] = G::Local(obj).rot;
            ShowRot(G::Local(obj).rot);
        }
    }

    auto skeletonObj = obj;
    NiAVObject* comObj;
    bool skeletonFound = false;
    while (G::Parent(skeletonObj))
    {
        if (G::NameIs(G::Parent(skeletonObj), COM_boneName)) {
            comObj = G::Parent(skeletonObj);
        }
        else if (G::NameIs(G::Parent(skeletonObj), skeletonNif_boneName)) {
            skeletonObj = G::Parent(skeletonObj);
            skeletonFound = true;
            break;
        }
        skeletonObj = G::Parent(skeletonObj);
    }
    if (skeletonFound == false) {
        logger.Error("Couldn't find skeleton for actor %08x\n", actor->formID);
        lastLocalDiff = NiPoint3(0, 0, 0);     // fo4-anatomy: nor one whose skeleton was not reached
        return;
    }


    objRotation = G::Local(skeletonObj).rot.Transpose();

	std::vector<long> thingIdList;
	std::vector<long> hashIdList;
    //logger.Info("Collisions on: %d, hashSize: %d\n", collisionsOn, hashSize);
	if (collisionsOn && hashSize>0 && !contact)
	{
		//logger.Info("Before Collision Stuff Start\n");
		// Collision Stuff Start
		for (int i = 0; i < thingCollisionSpheres.size(); i++)
		{
			thingCollisionSpheres[i].worldPos = G::World(obj).pos + (objRotation*thingCollisionSpheres[i].offset);
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
		std::vector<NiAVObject*> seenColliders;         // fo4-anatomy: each collider once per pass

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
                    // fo4-anatomy: a collider in two of this bone's grid cells pushed it twice; once per pass
                    if (std::find(seenColliders.begin(), seenColliders.end(), partitions[id].partitionCollisions[i].CollisionObject) != seenColliders.end())
                        continue;
                    seenColliders.push_back(partitions[id].partitionCollisions[i].CollisionObject);
                    // fo4-anatomy: a penis chain's balls collide as its tube ([Tube], TubeCollide.h), after this loop
                    if (IsTubeMember(partitions[id].partitionCollisions[i].colliderActor, partitions[id].partitionCollisions[i].colliderNodeName))
                        continue;

                    callCount++;

                    // Check if collision vector has changed
                    if (!CompareNiPoints(lastcollisionVector, collisionVector))
                    {
                        // Build thing's collision spheres for current frame
                        for (auto thingCollisSpheres : thingCollisionSpheres)
                        {
                            thingCollisSpheres.worldPos = G::World(obj).pos + (objRotation * thingCollisSpheres.offset) + collisionVector;
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
		{
			// fo4-anatomy: every partner's penis as one tube: one push each, its deepest ([Tube])
			NiPoint3 tubePush = zeroVector;
			if (TubePush(actor, boneName.c_str(), thingCollisionSpheres, tubePush)) {
				IsThereCollision = true;
				collisionVector = collisionVector + tubePush;
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
    logger.Error("bone %s for actor %08x with parent %s\n", boneName.c_str(), actor->formID, G::Name(skeletonObj));
//    ShowRot(G::World(skeletonObj).rot);
//    //ShowPos(G::World(G::Parent(obj)).rot.Transpose() * G::Local(obj).pos);
//#endif
    NiMatrix43 targetRot = G::Local(skeletonObj).rot.Transpose();
    NiPoint3 origWorldPos = (G::World(G::Parent(obj)).rot.Transpose() * origLocalPos[boneName.c_str()][actor->formID]) +  G::World(G::Parent(obj)).pos;

    // Offset to move Center of Mass make rotational motion more significant
    NiPoint3 target = (targetRot * NiPoint3(varCogOffsetX, varCogOffsetY, varCogOffsetZ)) + origWorldPos;

#if DEBUG
    logger.Error("World Position: ");
    ShowPos(G::World(obj).pos);
    logger.Error("oldWorldPos: ");
    ShowPos(oldWorldPos);
    logger.Error("target: ");
    ShowPos(target);
    //    //logger.Error("Parent World Position difference: ");
//    //ShowPos(G::World(obj).pos - G::World(G::Parent(obj)).pos);
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
            G::Local(obj).pos = origLocalPos[boneName.c_str()][actor->formID];
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

        if (contact && collisionsOn && hashSize > 0)
        {
            // fo4-anatomy (A-55): where the spring would put the bone, shown; out of every collider, the push turned
            // back into the spring's own units (the inverse of the shown-offset transform below), only the velocity
            // INTO the collider removed and a little friction along it. No kick: a steady press holds still.
            NiPoint3 cand = newPos + posDelta;
            NiMatrix43 rotLin;
            rotLin.SetEulerAngles(rotateLinearX * DEG_TO_RAD, rotateLinearY * DEG_TO_RAD, rotateLinearZ * DEG_TO_RAD);
            const NiMatrix43 parentRot = G::World(G::Parent(obj)).rot;
            const NiMatrix43 skelRot = G::Local(skeletonObj).rot;
            const NiPoint3 origLocal = origLocalPos[boneName.c_str()][actor->formID];
            auto shownWorld = [&](const NiPoint3& internal) {
                NiPoint3 d = internal - target;
                d.x = clamp(d.x, -maxOffsetX, maxOffsetX);
                d.y = clamp(d.y, -maxOffsetY, maxOffsetY);
                d.z = clamp(d.z - gravityCorrection, -maxOffsetZ, maxOffsetZ) + gravityCorrection;
                NiPoint3 l = skelRot * d;
                l.x *= linearX;
                l.y *= linearY;
                l.z *= linearZ;
                l = (rotLin * parentRot) * (skelRot.Transpose() * l);
                return G::World(G::Parent(obj)).pos + parentRot.Transpose() * (origLocal + l);
            };
            const NiPoint3 at = shownWorld(cand);
            NiPoint3 pushWorld = zeroVector;
            bool touched = false;
            for (int pass = 0; pass < 4; pass++) {         // several spheres at once settle in a few passes
                for (auto& s : thingCollisionSpheres)
                    s.worldPos = at + pushWorld + (objRotation * s.offset);
                NiPoint3 p = zeroVector;
                if (!ContactPush(actor, p))
                    break;
                touched = true;
                pushWorld += p;
            }
            if (touched) {
                NiPoint3 w = parentRot.Transpose() * (rotLin.Transpose() * (parentRot * pushWorld));
                NiPoint3 s = skelRot * w;
                s.x = linearX > 1e-4f ? s.x / linearX : 0.0f;
                s.y = linearY > 1e-4f ? s.y / linearY : 0.0f;
                s.z = linearZ > 1e-4f ? s.z / linearZ : 0.0f;
                const NiPoint3 dInternal = skelRot.Transpose() * s;
                cand += dInternal;
                const float len = std::sqrt(dInternal.x * dInternal.x + dInternal.y * dInternal.y + dInternal.z * dInternal.z);
                if (len > 1e-6f) {
                    const NiPoint3 n = dInternal * (1.0f / len);
                    float vn = velocity.x * n.x + velocity.y * n.y + velocity.z * n.z;
                    if (vn < 0.0f) {
                        velocity -= n * vn;
                        vn = 0.0f;
                    }
                    const float keep = std::exp(-8.0f * (float)originalDeltaT / 1000.0f);
                    velocity = n * vn + (velocity - n * vn) * keep;
                }
                collisionOnLastFrame = true;
            }
            newPos = cand;
        }
        else if (collisionsOn && hashSize > 0)
        {
            //LOG("Before Maybe Collision Stuff Start");
            NiPoint3 maybePos = newPos + posDelta;

            bool maybeNot = false;

            //After cbp movement collision detection
            thingIdList.clear();
            for (int i = 0; i < thingCollisionSpheres.size(); i++)
            {
                thingCollisionSpheres[i].worldPos = G::World(obj).pos + (objRotation * thingCollisionSpheres[i].offset) + posDelta;
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
            std::vector<NiAVObject*> seenColliders;     // fo4-anatomy: each collider once per pass
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
                        // fo4-anatomy: once per pass, and a tube's balls not one by one (as in the first pass)
                        if (std::find(seenColliders.begin(), seenColliders.end(), partitions[id].partitionCollisions[i].CollisionObject) != seenColliders.end())
                            continue;
                        seenColliders.push_back(partitions[id].partitionCollisions[i].CollisionObject);
                        if (IsTubeMember(partitions[id].partitionCollisions[i].colliderActor, partitions[id].partitionCollisions[i].colliderNodeName))
                            continue;

                        callCount++;

                        if (!CompareNiPoints(lastcollisionVector, collisionVector))
                        {
                            // Build thing's collision spheres for current frame after move changes
                            for (auto thingCollisSpheres : thingCollisionSpheres)
                            {
                                thingCollisSpheres.worldPos = G::World(obj).pos + (objRotation * thingCollisSpheres.offset) + maybePos + collisionVector;
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

            {
                // fo4-anatomy: the tubes, as in the first pass, at the moved position
                NiPoint3 tubePush = zeroVector;
                if (TubePush(actor, boneName.c_str(), thingCollisionSpheres, tubePush)) {
                    IsThereCollision = true;
                    maybeNot = true;
                    collisionVector = collisionVector + tubePush;
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

        // fo4-anatomy (A-55): rotateLinear on EVERY frame. OCBPC skipped it on a collision frame, so with a preset's
        // rotateLinear (OCBPC 0.3's [Butt] rotateLinearZ=90) a bone flickering in and out of contact swung its
        // offset between two directions frame after frame: a twitch under any hand.
        NiMatrix43 invRot;
        {
            NiMatrix43 rotateLinear;
            rotateLinear.SetEulerAngles(rotateLinearX* DEG_TO_RAD,
                                        rotateLinearY* DEG_TO_RAD,
                                        rotateLinearZ* DEG_TO_RAD);
            invRot = rotateLinear * G::World(G::Parent(obj)).rot;
        }

        auto localDiff = diff;
        localDiff = G::Local(skeletonObj).rot * localDiff;
        localDiff.x *= linearX;
        localDiff.y *= linearY;
        localDiff.z *= linearZ;

        auto rotDiff = localDiff;
        localDiff = G::Local(skeletonObj).rot.Transpose() * localDiff;

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
        ShowPos(G::Local(obj).pos);
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
        G::Local(obj).pos = newLocalPos;
        lastLocalDiff = localDiff;              // fo4-anatomy stretch groups read it after every bone ran

        if (absRotX) rotDiff.x = fabs(rotDiff.x);

        rotDiff.x *= rotationalX;
        rotDiff.y *= rotationalY;
        rotDiff.z *= rotationalZ;


//#if DEBUG
//        logger.Error("localTransform.pos after: ");
//        ShowPos(G::Local(obj).pos);
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
        G::Local(obj).rot = standardRot * origLocalRot[boneName.c_str()][actor->formID];

        if (!FiniteNear(G::Local(obj).pos) || !Finite(G::Local(obj).rot)) {
            G::Local(obj).pos = origLocalPos[boneName.c_str()][actor->formID];
            G::Local(obj).rot = origLocalRot[boneName.c_str()][actor->formID];
            oldWorldPos = target;
            velocity = NiPoint3(0, 0, 0);
            lastLocalDiff = NiPoint3(0, 0, 0);
            static std::unordered_set<std::string> said;
            const std::string key = std::to_string(actor->formID) + "|" + boneName.c_str();
            if (said.insert(key).second)
                spdlog::info("[physics] {:08X}: {} went non-finite or far off: back to its rest", actor->formID,
                    boneName.c_str());
        }

    //logger.Error("end update()\n");
    /*QueryPerformanceCounter(&endingTime);
    elapsedMicroseconds.QuadPart = endingTime.QuadPart - startingTime.QuadPart;
    elapsedMicroseconds.QuadPart *= 1000000000LL;
    elapsedMicroseconds.QuadPart /= frequency.QuadPart;
    _MESSAGE("Thing.update() Update Time = %lld ns\n", elapsedMicroseconds.QuadPart);*/

}

// fo4-anatomy (A-55): every collider touching this bone's spheres where they stand now, each once, the penis tubes as
// tubes (the same filters as Update's passes), summed as one push in world units.
bool Thing::ContactPush(Actor* actor, NiPoint3& push)
{
    push = zeroVector;
    std::vector<long> ids;
    for (auto& s : thingCollisionSpheres)
        for (long id : GetHashIdsFromPos(s.worldPos, s.radius, hashSize))
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
    bool hit = false;
    std::vector<NiAVObject*> seen;
    for (long id : ids) {
        auto part = partitions.find(id);
        if (part == partitions.end())
            continue;
        for (auto& col : part->second.partitionCollisions) {
            if (col.colliderActor == actor && std::strcmp(col.colliderNodeName.c_str(), boneName.c_str()) == 0)
                continue;
            if (col.isProp && !PropReaches(boneName.c_str()))
                continue;
            if (std::find(seen.begin(), seen.end(), col.CollisionObject) != seen.end())
                continue;
            seen.push_back(col.CollisionObject);
            if (IsTubeMember(col.colliderActor, col.colliderNodeName))
                continue;
            callCount++;
            bool colliding = false;
            NiPoint3 d = col.CheckCollision(colliding, thingCollisionSpheres, timeTick, 16, maxOffsetX, false);
            if (colliding) {
                hit = true;
                push += d;
            }
        }
    }
    NiPoint3 tube = zeroVector;
    if (TubePush(actor, boneName.c_str(), thingCollisionSpheres, tube)) {
        hit = true;
        push += tube;
    }
    return hit;
}
