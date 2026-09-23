#pragma once
#include <f4se\NiTypes.h>
#include <f4se\NiObjects.h>
#include <f4se\GameReferences.h>
#include <time.h>

#include "CollisionHub.h"
#include "config.h"

typedef std::unordered_map<const char*, std::unordered_map<UInt32, NiPoint3>> pos_map;
typedef std::unordered_map<const char*, std::unordered_map<UInt32, NiMatrix43>> rot_map;

class Thing {
    BSFixedString boneName;
    NiPoint3 oldWorldPos;
    float oldRotZ;
    NiPoint3 velocity;
    clock_t time;

public:
    Thing(Actor* actor, NiAVObject* obj, BSFixedString& name);
    ~Thing();

    float stiffness = 0.5f;
    float stiffness2 = 0.0f;
    float damping = 0.2f;
    float maxOffsetX = 5.0f;
    float maxOffsetY = 5.0f;
    float maxOffsetZ = 5.0f;
    float cogOffsetX = 0.0f;
    float cogOffsetY = 0.0f;
    float cogOffsetZ = 0.0f;

    float gravityBias = 0.0f;
    float gravityCorrection = 0.0f;
    float timeTick = 4.0f;
    float linearX = 0.0f;
    float linearY = 0.0f;
    float linearZ = 0.0f;
    float rotationalX = 0.0f;
    float rotationalY = 0.0f;
    float rotationalZ = 0.0f;

    float rotateLinearX = 0.0f;
    float rotateLinearY = 0.0f;
    float rotateLinearZ = 0.0f;

    float rotateRotationX = 0.0f;
    float rotateRotationY = 0.0f;
    float rotateRotationZ = 0.0f;

    float timeStep = 0.016f;

    bool absRotX = 0;

    // Stretch groups (fo4-anatomy): the bones of one opening share a group number (0 = none). When
    // ALL of them are pushed out further than stretchKnee - a big object in the opening, not a small
    // one off-centre, which pushes only one side - each moves its child node "<bone>_Stretch" out by
    // stretchGain x (the group's smallest push - stretchKnee), at most stretchMax, along its own push.
    // The body weights the stretch nodes, so an opening grows faster than the object past the knee.
    float stretchGroup = 0.0f;
    float stretchKnee = 0.0f;
    float stretchGain = 0.0f;
    float stretchMax = 0.0f;
    // the opening's axis in the parent's frame: only the push ACROSS it counts, so a hand pressing
    // on the opening from outside (a push along the axis) never stretches it; (0,0,0) = no axis
    NiPoint3 stretchAxis = NiPoint3(0, 0, 0);
    // this frame's displacement from rest, in the parent's frame (what Update added to origLocalPos)
    NiPoint3 lastLocalDiff = NiPoint3(0, 0, 0);

    static pos_map origLocalPos;
    static rot_map origLocalRot;

    NiAVObject* IsActorValid(Actor* actor);
    void Reset(Actor* actor);
    void Update(Actor* actor);	
    void UpdateConfig(configEntry_t& centry);
    
    void ShowPos(NiPoint3& p);
    void ShowRot(NiMatrix43& r);

	//Performance skip
	int skipFramesCount = 0;
	int skipFramesPelvisCount = 0;
	bool collisionOnLastFrame = false;


	NiPoint3 lastColliderPosition = zeroVector;

	std::vector<Sphere> thingCollisionSpheres;

	std::vector<Collision> ownColliders;


	std::vector<Sphere> CreateThingCollisionSpheres(Actor * actor, std::string nodeName, float nodescale);
};