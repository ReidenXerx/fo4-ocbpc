#include "f4se/NiNodes.h"
#include "f4se/GameForms.h"
#include "f4se/GameRTTI.h"

#include "ActorUtils.h"
#include "config.h"
#include "log.h"
#include "PapyrusOCBP.h"
#include "SimObj.h"

using actorUtils::IsBoneInWhitelist;
using actorUtils::IsActorInPowerArmor;

// Note we don't ref count the nodes becasue it's ignored when the Actor is deleted, and calling Release after that can corrupt memory
std::vector<std::string> boneNames;

SimObj::SimObj(Actor *actor, config_t &config)
    : things(4) {
    id = actor->formID;
}

SimObj::~SimObj() {
}


bool SimObj::Bind(Actor *actor, std::vector<std::string>& boneNames, config_t &config)
{
//	logger.error("bind\n");

    if (!actor) {
        return false;
    }
    auto loadedData = actor->unkF0;
    if (loadedData && loadedData->rootNode) {
        bound = true;

        things.clear();
        for (std::string b : boneNames) {
            const char* bone_c_str = b.c_str();
            BSFixedString cs(bone_c_str);
            auto bone = loadedData->rootNode->GetObjectByName(&cs);
            if (!bone) {
                logger.Info("Failed to find Bone %s for actor %08x\n", b.c_str(), actor->formID);
            } else {
                //logger.info("Doing Bone %s for actor %08x\n", b, actor->formID);
                things.emplace(b, Thing(actor, bone, cs));
            }
        }
        UpdateConfig(actor, std::vector<std::string>(), config);
        return  true;
    }
    return false;
}

void SimObj::Update(Actor *actor) {
    if (!bound)
        return;
    logger.Error("SimObj::Update\n");
    for (auto &t : things) {
        logger.Info("SimObj update: doing thing %s\n", t.first.c_str());
        
        // Might be a better way to do this
        if (boneIgnores.find(actor->formID) != boneIgnores.end()) {
            auto actorBoneMap = boneIgnores.at(actor->formID);
            if (actorBoneMap.find(t.first) != actorBoneMap.end()) {
                if (actorBoneMap.at(t.first)) {
                    continue;
                }
            }
        }

        if (!useWhitelist || (IsBoneInWhitelist(actor, t.first) && useWhitelist) &&
            !IsActorInPowerArmor(actor))
        {
            logger.Error("SimObj::Update - calling Thing::Update\n");
            t.second.Update(actor);
        }
    }
    UpdateStretch(actor);
    //logger.error("end SimObj update\n");
}

static float Length(const NiPoint3& p) {
    return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

// The part of a bone's push across its opening's axis (the whole push when no axis is set).
static NiPoint3 Across(const Thing& t) {
    const NiPoint3& d = t.lastLocalDiff;
    const NiPoint3& a = t.stretchAxis;
    float along = d.x * a.x + d.y * a.y + d.z * a.z;
    return d - a * along;
}

// fo4-anatomy stretch groups (see Thing.h): per group, the SMALLEST push across the opening among its
// bones says how big the object in the opening is; past the knee, every member moves its "<bone>_Stretch"
// child further out along its own push across the axis. A group needs two members on this actor: one bone
// alone cannot tell a big object from a small one beside it.
void SimObj::UpdateStretch(Actor* actor) {
    std::unordered_map<int, float> smallest;
    std::unordered_map<int, int> members;
    for (auto& t : things) {
        int g = (int)t.second.stretchGroup;
        if (g <= 0)
            continue;
        float d = Length(Across(t.second));
        auto it = smallest.find(g);
        if (it == smallest.end() || d < it->second)
            smallest[g] = d;
        members[g] += 1;
    }
    if (smallest.empty() || !actor || !actor->unkF0 || !actor->unkF0->rootNode)
        return;
    auto root = actor->unkF0->rootNode;
    for (auto& t : things) {
        int g = (int)t.second.stretchGroup;
        if (g <= 0)
            continue;
        BSFixedString childName((t.first + "_Stretch").c_str());
        NiAVObject* child = root->GetObjectByName(&childName);
        if (!child)
            continue;
        float extra = t.second.stretchGain * (smallest[g] - t.second.stretchKnee);
        if (members[g] < 2 || extra < 0.0f)
            extra = 0.0f;
        if (t.second.stretchMax > 0.0f && extra > t.second.stretchMax)
            extra = t.second.stretchMax;
        NiPoint3 across = Across(t.second);
        float m = Length(across);
        child->m_localTransform.pos = (m > 1e-4f && extra > 0.0f) ? across * (extra / m) : NiPoint3(0, 0, 0);
    }
}

bool SimObj::UpdateConfig(Actor* actor, std::vector<std::string>& boneNames, config_t& config) {
    logger.Error("SimObj::UpdateConfig\n");
    if (!actor) {
        return false;
    }
    auto loadedData = actor->unkF0;
    if (loadedData && loadedData->rootNode) {
        for (std::string b : boneNames) {
            logger.Error("SimObj::UpdateConfig - adding bone %s\n", b.c_str());
            BSFixedString cs(b.c_str());
            auto bone = loadedData->rootNode->GetObjectByName(&cs);
            auto findBone = things.find(b);
            if (!bone) {
                logger.Info("Failed to find Bone %s for actor %08x\n", b.c_str(), actor->formID);
            }
            else if (findBone == things.end()) {
                //logger.info("Doing Bone %s for actor %08x\n", b, actor->formID);
                things.emplace(b, Thing(actor, bone, cs));
            }
        }
    }
    for (auto &thing : things) {
        thing.second.UpdateConfig(config[std::string(thing.first)]);
    }
    return true;
}