// fo4-ocbpc: modified by fo4-anatomy (ReidenXerx), 2026-09-23: stretch groups.
// The original OpenCBP_FO4 / OCBPC code is under the MIT licence (LICENSE); these changes
// are under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once

#include <unordered_map>
#include <vector>
#include "Thing.h"
#include "Game.h"
#include "config.h"

#define NINODE_CHILDREN(ninode) ((NiTArray <NiAVObject *> *) ((char*)(&(ninode->m_children))))

class SimObj {
    UInt32 id = 0;
    bool bound = false;
public:
    std::unordered_map<std::string, Thing> things;
    bool anatomyOnly = false;   // fo4-anatomy (A-69): the preset's sex filter left him out; only our bones move
    SimObj(Actor *actor, config_t &config);
    SimObj() {}
    ~SimObj();
    bool Bind(Actor *actor, std::vector<std::string> &boneNames, config_t &config);
    void Update(Actor *actor);
    void UpdateStretch(Actor *actor);
    bool UpdateConfig(Actor* actor, std::vector<std::string>& boneNames, config_t& config);
    bool IsBound() { return bound; }
    // fo4-anatomy (h_wushen, 2026-11-06: 4.5 ms of a 7.2 ms frame with 12 actors): UpdateConfig looked every bone up
    // by name and re-read every setting by string key EVERY frame, for values that change only when the config loads.
    // Now: when the config generation or the armor choice changes, and every kConfigEvery frames (bones that appear
    // later, such as our genital nodes made after the bind), staggered by actor.
    bool ConfigDue(unsigned a_generation, bool a_armor, UInt32 a_formID);

private:
    static constexpr unsigned kConfigEvery = 30;
    unsigned cfgGeneration = ~0u;
    bool cfgArmor = false;
    unsigned cfgFrames = 0;
public:

};

extern std::vector<std::string> boneNames;