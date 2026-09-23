#include "config.h"
#include "INIReader.h"
#include "log.h"
#include "Mouth.h"
#include "SimObj.h"
#include "Thing.h"
#include "Utility.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

#include "f4se/GameObjects.h"
#include "f4se/GameRTTI.h"
#include "f4se_common/Utilities.h"

//#define DEBUG 0
#pragma warning(disable : 4996)

int configReloadCount = 60;
bool playerOnly = false;
bool femaleOnly = false;
bool maleOnly = false;
bool npcOnly = false;
bool detectArmor = false;
bool useWhitelist = false;

config_t config;
config_t configArmor;
configOverrides_t configOverrides;
configOverrides_t configArmorOverrides;

// TODO data structure these
whitelist_t whitelist;
std::vector<std::string> raceWhitelist;
std::unordered_map<UInt32, bool> armorIgnore;

std::vector<std::string> AffectedNodeLines;
std::vector<std::string> ColliderNodeLines;
std::vector<ConfigLine> AffectedNodesList; //Nodes that can be collided with
std::vector<ConfigLine> ColliderNodesList; //Nodes that can collide nodes

int collisionSkipFrames = 0; //0
int gridsize = 25;
int adjacencyValue = 0;
int tuningModeCollision = 0;
float actorDistance = 4194304.0f;
int logging = 0;

float collisionX = 1.0;
float collisionY = 1.0;
float collisionZ = 1.0;

// fo4-anatomy props (ocbp.ini [Props]): what an animation puts in a hand collides along its length
std::vector<std::string> propNodes;
float propRadius = 1.6f;
float propSpacing = 1.5f;
float propMaxLength = 40.0f;
float propMinBound = 1.0f;

bool LoadConfig() {
    logger.Info("loadConfig\n");

    std::set<std::string> bonesSet;

    bool reloadActors = false;
    auto playerOnlyOld = playerOnly;
    auto femaleOnlyOld = femaleOnly;
    auto maleOnlyOld = maleOnly;
    auto npcOnlyOld = npcOnly;
    auto useWhitelistOld = useWhitelist;

    boneNames.clear();
    config.clear();
    configArmor.clear();
    configOverrides.clear();
    configArmorOverrides.clear();
    armorIgnore.clear();

    // Note: Using INIReader results in a slight double read
    INIReader configReader("Data\\F4SE\\Plugins\\ocbp.ini");
    if (configReader.ParseError() < 0) {
        logger.Error("Can't load 'ocbp.ini'\n");
    }
    logger.Error("Reading CBP Config\n");

    // Read general settings
    playerOnly = configReader.GetBoolean("General", "playerOnly", false);
    npcOnly    = configReader.GetBoolean("General", "npcOnly", false);
    useWhitelist = configReader.GetBoolean("General", "useWhitelist", false);

    if (useWhitelist) {
        maleOnly = false;
        femaleOnly = false;
    }
    else {
        femaleOnly = configReader.GetBoolean("General", "femaleOnly", false);
        maleOnly = configReader.GetBoolean("General", "maleOnly", false);
    }

    reloadActors = (playerOnly ^ playerOnlyOld) ||
                    (femaleOnly ^ femaleOnlyOld) ||
                    (maleOnly ^ maleOnlyOld) ||
                    (npcOnly ^ npcOnlyOld) ||
                    (useWhitelist ^ useWhitelistOld);

    detectArmor = configReader.GetBoolean("General", "detectArmor", false);
    configReloadCount = configReader.GetInteger("Tuning", "rate", 0);

    // fo4-anatomy props: attach nodes, comma-separated; none listed = the feature is off
    propNodes.clear();
    {
        std::stringstream nodes(configReader.Get("Props", "nodes", ""));
        std::string node;
        while (std::getline(nodes, node, ',')) {
            node.erase(0, node.find_first_not_of(" \t"));
            node.erase(node.find_last_not_of(" \t") + 1);
            if (!node.empty())
                propNodes.push_back(node);
        }
    }
    propRadius = (float)configReader.GetReal("Props", "radius", 1.6);
    propSpacing = (float)configReader.GetReal("Props", "spacing", 1.5);
    propMaxLength = (float)configReader.GetReal("Props", "maxLength", 40.0);
    propMinBound = (float)configReader.GetReal("Props", "minBound", 1.0);
    if (propSpacing < 0.25f)
        propSpacing = 0.25f;

    //Read armorIgnore
    auto armorIgnoreStr = configReader.Get("General", "armorIgnore", "");
    {
        size_t commaPos;
        do {
            commaPos = armorIgnoreStr.find_first_of(",");
            auto token = armorIgnoreStr.substr(0, commaPos);
            UInt32 formID;
            std::stringstream ss;
            ss << std::hex << token;
            ss >> formID;
            armorIgnore[formID] = true;
            armorIgnoreStr = armorIgnoreStr.substr(commaPos + 1);

            //logger.Info("<token:> %s, <rest:> %s, <commaPos:> %d, <colonPos:> %d\n", token.c_str(), whitelistName.c_str(), commaPos >= 0, colonPos < 0);
        } while (commaPos != -1);
    }

    // Read sections
    auto sections = configReader.Sections();
    for (auto sectionsIter = sections.begin(); sectionsIter != sections.end(); ++sectionsIter) {

        // Split for override section check
        auto overrideStr = std::string("Override:");
        auto splitStr = std::mismatch(overrideStr.begin(), overrideStr.end(), sectionsIter->begin());

        auto overrideAStr = std::string("Override.A:");
        auto splitAStr = std::mismatch(overrideAStr.begin(), overrideAStr.end(), sectionsIter->begin());

        if (*sectionsIter == std::string("Attach")) {
            // Get section contents
            auto sectionMap = configReader.Section(*sectionsIter);
            for (auto& valuesIter : sectionMap) {
                auto& boneName = valuesIter.first;
                auto& attachName = valuesIter.second;
                boneNames.push_back(boneName);
                // Find specified bone section and insert map values into config
                if (sections.find(attachName) != sections.end()) {
                    auto attachMapSection = configReader.Section(attachName);
                    for (auto& attachIter : attachMapSection) {
                        auto& keyName = attachIter.first;
                        config[boneName][keyName] = configReader.GetFloat(attachName, keyName, 0.0);
                    }
                }
            }
        }
        else if (*sectionsIter == std::string("Attach.A") && detectArmor) {
            // Get section contents
            auto sectionMap = configReader.Section(*sectionsIter);
            for (auto &valuesIter : sectionMap) {
                auto &boneName = valuesIter.first;
                auto &attachName = valuesIter.second;
                boneNames.push_back(boneName);
                // Find specified bone section and insert map values into configArmor
                if (sections.find(attachName) != sections.end()) {
                    auto attachMapSection = configReader.Section(attachName);
                    for (auto &attachIter : attachMapSection) {
                        auto& keyName = attachIter.first;
                        configArmor[boneName][keyName] = configReader.GetFloat(attachName, keyName, 0.0);
                    }
                }
            }
        }
        else if (*sectionsIter == std::string("Whitelist") && useWhitelist) {
            whitelist.clear();
            raceWhitelist.clear();

            // Get section contents
            auto sectionMap = configReader.Section(*sectionsIter);
            for (auto& valuesIter : sectionMap) {
                auto& boneName      = valuesIter.first;
                auto& whitelistName = valuesIter.second;

                size_t commaPos;
                do {
                    commaPos = whitelistName.find_first_of(",");
                    auto token = whitelistName.substr(0, commaPos);
                    size_t colonPos = token.find_last_of(":");
                    auto raceName = token.substr(0, colonPos);
                    auto genderStr = token.substr(colonPos + 1);

                    if (colonPos == -1) {
                        whitelist[boneName][token].male = true;
                        whitelist[boneName][token].female = true;
                        raceWhitelist.push_back(token);
                    }
                    else if (genderStr == "male") {
                        whitelist[boneName][raceName].male = true;
                        raceWhitelist.push_back(raceName);
                    }
                    else if (genderStr == "female") {
                        whitelist[boneName][raceName].female = true;
                        raceWhitelist.push_back(raceName);
                    }
                    whitelistName = whitelistName.substr(commaPos + 1);

                    //logger.Info("<token:> %s, <rest:> %s, <commaPos:> %d, <colonPos:> %d\n", token.c_str(), whitelistName.c_str(), commaPos >= 0, colonPos < 0);
                } while (commaPos != -1);
            }
        }
        else if (splitStr.first == overrideStr.end()) {
            // If section name is prefixed with "Override:", grab other half of name for bone
            auto boneName = std::string(splitStr.second, sectionsIter->end());

            // Get section contents
            auto sectionMap = configReader.Section(*sectionsIter); 
            for (auto &valuesIt : sectionMap) {
                configOverrides[boneName][valuesIt.first] = configReader.GetFloat(*sectionsIter, valuesIt.first, 0.0);
            }
        }
        else if (splitAStr.first == overrideAStr.end()) {
            // If section name is prefixed with "Override:", grab other half of name for bone
            auto boneName = std::string(splitAStr.second, sectionsIter->end());

            // Get section contents
            auto sectionMap = configReader.Section(*sectionsIter);
            for (auto& valuesIt : sectionMap) {
                configArmorOverrides[boneName][valuesIt.first] = configReader.GetFloat(*sectionsIter, valuesIt.first, 0.0);
            }
        }
    }

    // replace configs with override settings (if any)
    for (auto &boneIter : configOverrides) {
        if (config.count(boneIter.first) > 0) {
            for (auto settingIter : boneIter.second) {
                config[boneIter.first][settingIter.first] = settingIter.second;
            }
        }
    }

    // replace armor configs with override settings (if any)
    for (auto& boneIter : configArmorOverrides) {
        if (configArmor.count(boneIter.first) > 0) {
            for (auto settingIter : boneIter.second) {
                configArmor[boneIter.first][settingIter.first] = settingIter.second;
            }
        }
    }

    // Remove duplicate entries
    bonesSet = std::set<std::string>(boneNames.begin(), boneNames.end());
    boneNames.assign(bonesSet.begin(), bonesSet.end());

    logger.Error("Finished CBP Config\n");
    LoadMouthConfig(configReader);

    return reloadActors;
}

void LoadCollisionConfig()
{
    AffectedNodeLines.clear();
    ColliderNodeLines.clear();
    AffectedNodesList.clear();
    ColliderNodesList.clear();

    std::string filepath = "Data\\F4SE\\Plugins\\OCBPCollisionConfig.txt";

    std::ifstream file(filepath);

    if (!file.is_open())
    {
        transform(filepath.begin(), filepath.end(), filepath.begin(), ::tolower);
        file.open(filepath);
    }

    if (file.is_open())
    {
        std::string line;
        std::string currentSetting;
        while (std::getline(file, line))
        {
            //trim(line);
            skipComments(line);
            trim(line);
            if (line.length() > 0)
            {
                if (line.substr(0, 1) == "[")
                {
                    //newsetting
                    currentSetting = line;
                }
                else
                {
                    if (currentSetting == "[ExtraOptions]")
                    {
                        std::string variableName;
                        float variableValue = GetConfigSettingsFloatValue(line, variableName);
                        if (variableName == "collisionX")
                        {
                            collisionX = variableValue;
                        }
                        else if (variableName == "collisionY")
                        {
                            collisionY = variableValue;
                        }
                        else if (variableName == "collisionZ")
                        {
                            collisionZ = variableValue;
                        }
                        else if (variableName == "adjacencyValue")
                        {
                            adjacencyValue = variableValue;
                        }
                    }
                    else if (currentSetting == "[AffectedNodes]")
                    {
                        AffectedNodeLines.emplace_back(line);
                        ConfigLine newConfigLine;
                        newConfigLine.NodeName = line;
                        AffectedNodesList.emplace_back(newConfigLine);
                    }
                    else if (currentSetting == "[ColliderNodes]")
                    {
                        ColliderNodeLines.emplace_back(line);
                        ConfigLine newConfigLine;
                        newConfigLine.NodeName = line;
                        ColliderNodesList.emplace_back(newConfigLine);
                    }
                    else
                    {
                        Sphere newSphere;
                        ConfigLineSplitter(line, newSphere);

                        std::string trimmedSetting = gettrimmed(currentSetting);
                        if (std::find(AffectedNodeLines.begin(), AffectedNodeLines.end(), trimmedSetting) != AffectedNodeLines.end())
                        {
                            for (int i = 0; i < AffectedNodesList.size(); i++)
                            {
                                if (AffectedNodesList[i].NodeName == trimmedSetting)
                                {
                                    newSphere.NodeName = AffectedNodesList[i].NodeName;
                                    AffectedNodesList[i].CollisionSpheres.emplace_back(newSphere);
                                    break;
                                }
                            }
                        }
                        if (std::find(ColliderNodeLines.begin(), ColliderNodeLines.end(), trimmedSetting) != ColliderNodeLines.end())
                        {
                            for (int i = 0; i < ColliderNodesList.size(); i++)
                            {
                                if (ColliderNodesList[i].NodeName == trimmedSetting)
                                {
                                    newSphere.NodeName = ColliderNodesList[i].NodeName;
                                    ColliderNodesList[i].CollisionSpheres.emplace_back(newSphere);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        DumpCollisionConfigsToLog();
        logger.Info("Collision Config file is loaded successfully.\n");
        return;

    }

    logger.Info("Collision Config file is not loaded.\n");
    return;
}

void ConfigLineSplitter(std::string& line, Sphere& newSphere)
{
    std::vector<std::string> splittedFloats = split(line, ',');
    if (splittedFloats.size() > 0)
    {
        newSphere.offset.x = strtof(splittedFloats[0].c_str(), 0);
    }
    if (splittedFloats.size() > 1)
    {
        newSphere.offset.y = strtof(splittedFloats[1].c_str(), 0);
    }
    if (splittedFloats.size() > 2)
    {
        newSphere.offset.z = strtof(splittedFloats[2].c_str(), 0);
    }
    if (splittedFloats.size() > 3)
    {
        newSphere.radius = strtof(splittedFloats[3].c_str(), 0);
    }
}

int GetConfigSettingsValue(std::string line, std::string& variable)
{
    int value = 0;
    std::vector<std::string> splittedLine = split(line, '=');
    variable = "";
    if (splittedLine.size() > 1)
    {
        variable = splittedLine[0];
        trim(variable);

        std::string valuestr = splittedLine[1];
        trim(valuestr);
        value = std::stoi(valuestr);
    }

    return value;
}

float GetConfigSettingsFloatValue(std::string line, std::string& variable)
{
    float value = 0;
    std::vector<std::string> splittedLine = split(line, '=');
    variable = "";
    if (splittedLine.size() > 1)
    {
        variable = splittedLine[0];
        trim(variable);

        std::string valuestr = splittedLine[1];
        trim(valuestr);
        value = strtof(valuestr.c_str(), 0);
    }

    return value;
}

std::string GetConfigSettingsStringValue(std::string line, std::string& variable)
{
    std::string valuestr = "";
    std::vector<std::string> splittedLine = split(line, '=');
    variable = "";
    if (splittedLine.size() > 0)
    {
        variable = splittedLine[0];
        trim(variable);
    }

    if (splittedLine.size() > 1)
    {
        valuestr = splittedLine[1];
        trim(valuestr);
    }

    return valuestr;
}

void printSpheresMessage(std::string message, std::vector<Sphere> spheres)
{
    for (int i = 0; i < spheres.size(); i++)
    {
        message += " Spheres: ";
        message += std::to_string(spheres[i].offset.x);
        message += ",";
        message += std::to_string(spheres[i].offset.y);
        message += ",";
        message += std::to_string(spheres[i].offset.z);
        message += ",";
        message += std::to_string(spheres[i].radius);
    }
    logger.Info("%s\n", message.c_str());
}

std::vector<std::string> ConfigLineVectorToStringVector(std::vector<ConfigLine> linesList)
{
    std::vector<std::string> outVector;

    for (int i = 0; i < linesList.size(); i++)
    {
        std::string str = linesList[i].NodeName;
        trim(str);
        outVector.emplace_back(str);
    }
    return outVector;
}

void DumpConfigsToLog()
{
    // Log contents of config
    logger.Info("***** Config Dump *****\n");
    for (auto section : config) {
        logger.Info("[%s]\n", section.first.c_str());
        for (auto setting : section.second) {
            logger.Info("%s=%f\n", setting.first.c_str(), setting.second);
        }
    }

    logger.Info("***** ConfigArmor Dump *****\n");
    for (auto section : configArmor) {
        logger.Info("[%s]\n", section.first.c_str());
        for (auto setting : section.second) {
            logger.Info("%s=%f\n", setting.first.c_str(), setting.second);
        }
    }
}

void DumpWhitelistToLog() {
    logger.Info("***** Whitelist Dump *****\n");
    for (auto section : whitelist) {
        logger.Info("[%s]\n", section.first.c_str());
        for (auto setting : section.second) {
            logger.Info("%s= female: %d, male: %d\n", setting.first.c_str(), setting.second.female, setting.second.male);
        }
    }
}

void DumpCollisionConfigsToLog() {

    logger.Info("***** Affected Node Lines Dump *****\n");
    for (auto line : AffectedNodeLines)
    {
        logger.Info("%s\n", line.c_str());
    }

    logger.Info("***** Collider Node Lines Dump *****\n");
    for (auto line : ColliderNodeLines)
    {
        logger.Info("%s\n", line.c_str());
    }

    logger.Info("***** Affected Nodes Dump *****\n");
    for (auto node : AffectedNodesList)
    {
        logger.Info("%s\n", node.NodeName.c_str());
        printSpheresMessage("", node.CollisionSpheres);
    }

    logger.Info("***** Collider Nodes Dump *****\n");
    for (auto node : ColliderNodesList)
    {
        logger.Info("%s\n", node.NodeName.c_str());
        printSpheresMessage("", node.CollisionSpheres);
    }
}