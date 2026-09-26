#pragma once
// fo4-anatomy: the Runtime Database build's precompiled header (CommonLibF4RD).

#pragma warning(push)
#include "F4SE/F4SE.h"
#include "RE/Fallout.h"

#include <spdlog/sinks/basic_file_sink.h>
#pragma warning(pop)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define DLLEXPORT __declspec(dllexport)

namespace rdlog = F4SE::log;   // the classic code has its own `logger` (log.h)

using namespace std::literals;

#include "Game.h"   // the classic code's view of the game, on CommonLibF4RD
