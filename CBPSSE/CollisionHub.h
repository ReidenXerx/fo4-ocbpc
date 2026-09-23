// fo4-ocbpc: modified by fo4-anatomy (ReidenXerx), 2026-09-23: prop colliders and the discovery log.
// The original OpenCBP_FO4 / OCBPC code is under the MIT licence (LICENSE); these changes
// are under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once

#include "Collision.h"
#include "ActorEntry.h"
#include "f4se\PapyrusActor.h"


extern int gridsize;
extern int a;
extern int b;
extern int c;

extern long hashSize;

extern std::vector<Collision> otherColliders;



extern int callCount;


void CreateOtherColliders();

// fo4-anatomy discovery log (anatomy_ocbpc.log): writes the line once per key
void AnatomyLogLine(const std::string& key, const char* line);

void UpdateColliderPositions(std::vector<Collision> &colliderList);



struct Partition
{
	std::vector<Collision> partitionCollisions;
};

typedef std::unordered_map<long, Partition> PartitionMap;

extern PartitionMap partitions;

long GetHashIdFromPos(NiPoint3 pos, long size);

std::vector<long> GetHashIdsFromPos(NiPoint3 pos, float radius, long size);

bool CheckPelvisArmor(Actor* actor);



