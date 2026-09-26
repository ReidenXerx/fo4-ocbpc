#pragma once
#include <vector>

struct ActorEntry {
    UInt32 id;
    Actor* actor;
};
extern std::vector<ActorEntry> actorEntries;
