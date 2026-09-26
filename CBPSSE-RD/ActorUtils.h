#pragma once
#include "config.h"

#include "Game.h"
namespace actorUtils {
    std::string GetActorRaceEID(Actor* actor);
    bool IsActorInPowerArmor(Actor* actor);
    bool IsActorTorsoArmorEquipped(Actor* actor);
    bool IsActorMale(Actor* actor);
    bool IsActorTrackable(Actor* actor);
    bool IsActorValid(Actor* actor);
    bool IsBoneInWhitelist(Actor* actor, std::string boneName);
}