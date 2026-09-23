#pragma once
#include <unordered_map>
#include <vector>

#include "f4se/GameReferences.h"
#include "f4se/GameEvents.h"

enum eLogLevels
{
	LOGLEVEL_ERR = 0,
	LOGLEVEL_WARN,
	LOGLEVEL_INFO,
};

class Configuration {
};

struct whitelistSex {
    bool male;
    bool female;
};

typedef std::unordered_map<std::string, float> configEntry_t;
typedef std::unordered_map<std::string, configEntry_t> config_t;
typedef std::unordered_map<std::string, configEntry_t> configOverrides_t;
typedef std::unordered_map<std::string, std::unordered_map<std::string, whitelistSex>> whitelist_t;

extern bool playerOnly;
extern bool femaleOnly;
extern bool maleOnly;
extern bool npcOnly;
extern bool detectArmor;
extern bool useWhitelist;

extern int configReloadCount;
extern config_t config;
extern config_t configArmor;
extern whitelist_t whitelist;
extern std::vector<std::string> raceWhitelist;
extern std::unordered_map<UInt32, bool> armorIgnore;
bool LoadConfig();
void DumpWhitelistToLog();


//void Log(const int msgLogLevel, const char* fmt, ...);

//#define LOG(fmt, ...) Log(LOGLEVEL_WARN, fmt, ##__VA_ARGS__)
//#define LOG_ERR(fmt, ...) Log(LOGLEVEL_ERR, fmt, ##__VA_ARGS__)
//#define LOG_INFO(fmt, ...) Log(LOGLEVEL_INFO, fmt, ##__VA_ARGS__)

//Collision Stuff

struct Sphere
{
	NiPoint3 offset = NiPoint3(0, 0, 0);
	double radius = 4.0;
	double radiuspwr2 = 16.0;
	NiPoint3 worldPos = NiPoint3(0, 0, 0);
	std::string NodeName;
};

struct ConfigLine
{
	std::vector<Sphere> CollisionSpheres;
	std::string NodeName;
};

extern int collisionSkipFrames;

extern int gridsize;
extern int adjacencyValue;
extern int tuningModeCollision;
extern float actorDistance;

extern float collisionX;
extern float collisionY;
extern float collisionZ;

// fo4-anatomy props (ocbp.ini [Props])
extern std::vector<std::string> propNodes;
extern float propRadius;
extern float propSpacing;
extern float propMaxLength;
extern float propMinBound;

extern std::vector<std::string> AffectedNodeLines;
extern std::vector<std::string> ColliderNodeLines;

extern std::vector<ConfigLine> AffectedNodesList; //Nodes that can be collided with
extern std::vector<ConfigLine> ColliderNodesList; //Nodes that can collide nodes

void LoadCollisionConfig();

void ConfigLineSplitter(std::string &line, Sphere &newSphere);

int GetConfigSettingsValue(std::string line, std::string &variable);
std::string GetConfigSettingsStringValue(std::string line, std::string &variable);

float GetConfigSettingsFloatValue(std::string line, std::string &variable);

void printSpheresMessage(std::string message, std::vector<Sphere> spheres);

std::vector<std::string> ConfigLineVectorToStringVector(std::vector<ConfigLine> linesList);

void DumpCollisionConfigsToLog();

//bool IsActorMale(Actor* actor);

//bool RegisterFuncs(VMClassRegistry* registry);
//BSFixedString GetVersion(StaticFunctionTag* base);
//BSFixedString GetVersionMinor(StaticFunctionTag* base);
//BSFixedString GetVersionBeta(StaticFunctionTag* base);