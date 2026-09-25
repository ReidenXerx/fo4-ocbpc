// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-26: penis chains collide as one tube (Tube.h).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// ocbp.ini [Tube] (Anatomy's):
//   enabled=0          1: every chain below collides as one tube, and its bones' own balls no longer do
//   chains=a|b|c/...   bones, root first, one chain per list (a node with no collider sphere is skipped)
//   skin=0.2           the collider sphere less this is the flesh (the mouth's [Mouth] skin, the same fact)
//   glans=<node>, glansProfile=...   the glans's profile on that tip bone (Glans.h; [Mouth] has the same)
//   props=1            a toy on a [Props] node collides as one tube too, instead of its line of balls
// Built each frame after the colliders move (scan.cpp); read by every Thing's collision passes, on the
// same thread.
#include "INIReader.h"
#include "config.h"

#include "f4se/GameReferences.h"

#include <string>
#include <vector>

void LoadTubeConfig(INIReader& reader);
void BuildTubes();
// A collider node that is part of a tube this frame: the per-sphere collision passes skip it
bool IsTubeMember(const Actor* owner, const std::string& node);
// The push the tubes give these spheres of `bone`: one per tube (its deepest), added over tubes. A penis
// never pushes its own owner; a toy pushes only the [Props] targets, its holder's included (Tube::Reaches)
bool TubePush(const Actor* self, const char* bone, const std::vector<Sphere>& spheres, NiPoint3& push);
