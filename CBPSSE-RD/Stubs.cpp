// fo4-anatomy: phase 1 of the Runtime Database port - the mouth, Rapport's face authority and the glances are not
// ported yet (phase 2: their hooks need per-runtime ids and layout proofs). Until then they do nothing, on every
// runtime: never a half-ported hook.
#include "PCH.h"
#include "Game.h"
#include "Mouth.h"
#include "Eyes.h"
#include "ActorEntry.h"

void LoadMouthConfig(INIReader&) {}
void InstallMouthHook() {}
void UpdateMouths() {}
bool MouthOpening(Actor*, NiPoint3&, NiPoint3&, NiPoint3*) { return false; }
void LoadFaceConfig(INIReader&) {}
void ListenForFaces(const F4SE::MessagingInterface*) {}
void SayFaceHello() {}
void ReleaseAllFaces(const char*) {}
void RefreshHeldFaces() {}
void StartFaceAuthorityTest() {}

void LoadEyeConfig(INIReader&) {}
void InstallEyeHook() {}
bool EyesTurn() { return false; }
float EyeLidMax(unsigned int) { return 1.0f; }
unsigned long long EyeClockMs() { return 0; }
void UpdateEyeProbe(const std::vector<ActorEntry>&, float) {}
