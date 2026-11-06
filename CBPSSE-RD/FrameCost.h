// fo4-anatomy (2026-11-06): what the engine's frame costs, by part (h_wushen via Watcher: 140 fps without the engine,
// 70 with it, 12 people near him in the Museum of Freedom). UpdateActors laps each part; every 1800 frames cbp.log
// gets one line, and the health check prints the running numbers.
#pragma once
#include <string>

namespace FrameCost
{
	enum Part
	{
		kScan,       // the candidates: who is near, who is tracked
		kAim,        // UpdateAims
		kSound,      // Sound::Update
		kColliders,  // CreateOtherColliders + UpdateColliderPositions
		kMouth,      // UpdateMouths
		kTubes,      // BuildTubes
		kHash,       // the colliders' spatial hash
		kLoaded,     // EnsureLoadedActors (our bones on every loaded actor)
		kTicks,      // Health, Strands, Fluid
		kBones,      // EnsureAnatomyBones per simulated actor
		kPhysics,    // OCBPC's own physics per actor (config + update or bind)
		kSkirt,      // UpdateSkirt
		kCanal,      // UpdateCanal
		kOther,      // the rest (config reload)
		kConfig,     // SimObj::UpdateConfig (bone lookups + settings), when due
		kParts
	};

	void BeginFrame();
	void Lap(Part a_part);       // the time since the last lap (or BeginFrame) goes to this part
	void EndFrame(size_t a_actors);
	std::string Text();          // "1.23 ms per frame on average ...": the running window, or the last full one
}
