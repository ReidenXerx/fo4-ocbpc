#pragma once
#include "config.h"

#include "Game.h"
namespace actorUtils {
    std::string GetActorRaceEID(Actor* actor);
    bool IsActorInPowerArmor(Actor* actor);
    bool IsActorTorsoArmorEquipped(Actor* actor);
    bool IsActorMale(Actor* actor);
    // Servitron (Nexus 32801): a female-bodied robot race with openings of its own (fo4-anatomy's rigged rubber abdomen)
    bool IsServitron(Actor* actor);
    bool ServitronIsMale(Actor* actor);   // wears fo4-anatomy's male rubber abdomen
    bool IsActorTrackable(Actor* actor);
    // fo4-anatomy (A-69): every filter of the preset but its sex one (femaleOnly / maleOnly)
    bool IsActorTrackableForAnatomy(Actor* actor);
    bool IsActorValid(Actor* actor);
    bool IsBoneInWhitelist(Actor* actor, std::string boneName);
}