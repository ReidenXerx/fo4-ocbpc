#pragma once

#include <unordered_map>
#include <string>


extern std::unordered_map<UInt32, std::unordered_map<std::string, bool>> boneIgnores; // probably should be moved somewhere else

namespace papyrusOCBP
{
    void RegisterFuncs(RE::BSScript::IVirtualMachine* vm);
};