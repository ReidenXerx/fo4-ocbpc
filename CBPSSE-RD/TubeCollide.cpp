// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-26: penis chains collide as one tube (Tube.h).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "TubeCollide.h"

#include "Game.h"
#include "CollisionHub.h"
#include "Glans.h"
#include "Tube.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace
{
	bool enabled = false;
	std::vector<std::vector<std::string>> chainNames;
	float skin = 0.2f;
	bool propsToo = true;
	std::string glansNode;
	std::vector<Glans::Step> glansProfile;

	struct Point
	{
		NiPoint3 pos;
		float r;
	};
	struct Chain
	{
		const Actor* owner;
		bool prop;                              // a toy on a [Props] node (Tube::Reaches)
		std::vector<Point> pts;
		NiPoint3 lo, hi;                        // its bounds, with its widest radius
	};
	std::vector<Chain> tubes;                   // this frame's
	std::vector<std::pair<const Actor*, std::string>> members;

	std::vector<std::string> Split(const std::string& text, char sep)
	{
		std::vector<std::string> out;
		std::stringstream in(text);
		std::string part;
		while (std::getline(in, part, sep)) {
			part.erase(0, part.find_first_not_of(" \t"));
			part.erase(part.find_last_not_of(" \t") + 1);
			if (!part.empty())
				out.push_back(part);
		}
		return out;
	}

	void Note(const std::string& key, const char* fmt, ...)
	{
		if (AnatomyLogSeen(key))
			return;
		char line[512];
		va_list args;
		va_start(args, fmt);
		_vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
		va_end(args);
		AnatomyLogLine(key, line);
	}
}

void LoadTubeConfig(INIReader& reader)
{
	enabled = reader.GetBoolean("Tube", "enabled", false);
	chainNames.clear();
	for (auto& chain : Split(reader.Get("Tube", "chains", ""), '/')) {
		auto names = Split(chain, '|');
		if (names.size() >= 2)
			chainNames.push_back(names);
	}
	skin = (std::max)(0.0f, (float)reader.GetReal("Tube", "skin", skin));
	propsToo = reader.GetBoolean("Tube", "props", true);
	glansNode = reader.Get("Tube", "glans", "");
	glansProfile.clear();
	for (auto& step : Split(reader.Get("Tube", "glansProfile", ""), ',')) {
		auto parts = Split(step, ':');
		if (parts.size() != 2) {
			glansProfile.clear();
			break;
		}
		glansProfile.push_back(Glans::Step{ (float)std::atof(parts[0].c_str()), (float)std::atof(parts[1].c_str()) });
	}
	if (!Glans::Valid(glansProfile) || glansNode.empty())
		glansProfile.clear();
	if (chainNames.empty() && !propsToo)
		enabled = false;                        // toys alone may still want their tube (release review)
	Note("tube|config|" + std::to_string((int)enabled) + "|" + std::to_string(chainNames.size()),
		"[tube] %s: %d chain(s), skin %.2f, glans %s (%d step(s)); their bones' own balls %s\n",
		enabled ? "on" : "off", (int)chainNames.size(), skin, glansNode.empty() ? "-" : glansNode.c_str(),
		(int)glansProfile.size(), enabled ? "no longer collide one by one" : "collide as before");
	Note("tube|props|" + std::to_string((int)(enabled && propsToo)), "[tube] toys: %s\n",
		enabled && propsToo ? "each collides as one tube (its line of balls no longer pushes one by one)"
		                    : "collide as their line of balls");
}

void BuildTubes()
{
	tubes.clear();
	members.clear();
	if (!enabled)
		return;
	std::unordered_map<const Actor*, std::unordered_map<std::string, const Collision*>> byActor;
	for (auto& c : otherColliders)
		if (c.colliderActor && !c.isProp && !c.collisionSpheres.empty())
			byActor[c.colliderActor][c.colliderNodeName] = &c;
	for (auto& actorNodes : byActor) {
		for (auto& names : chainNames) {
			Chain ch{ actorNodes.first, false, {}, NiPoint3(), NiPoint3() };
			std::vector<std::string> used;
			bool tipFound = false;
			for (auto& n : names) {
				auto it = actorNodes.second.find(n);
				tipFound = it != actorNodes.second.end();
				if (!tipFound)
					continue;
				used.push_back(n);
				for (auto& s : it->second->collisionSpheres)
					ch.pts.push_back(Point{ s.worldPos, (float)s.radius });
			}
			if (ch.pts.size() < 2)
				continue;                           // a lone ball collides as it always did
			if (tipFound && !glansProfile.empty() && names.back() == glansNode)
				Glans::Shape(ch.pts, glansProfile, skin);
			float widest = 0.0f;
			for (auto& p : ch.pts) {
				p.r = (std::max)(0.2f, p.r - skin);  // the flesh
				widest = (std::max)(widest, p.r);
			}
			ch.lo = ch.hi = ch.pts.front().pos;
			for (auto& p : ch.pts) {
				ch.lo = NiPoint3((std::min)(ch.lo.x, p.pos.x), (std::min)(ch.lo.y, p.pos.y), (std::min)(ch.lo.z, p.pos.z));
				ch.hi = NiPoint3((std::max)(ch.hi.x, p.pos.x), (std::max)(ch.hi.y, p.pos.y), (std::max)(ch.hi.z, p.pos.z));
			}
			ch.lo = ch.lo - NiPoint3(widest, widest, widest);
			ch.hi = ch.hi + NiPoint3(widest, widest, widest);
			tubes.push_back(ch);
			for (auto& n : used)
				members.emplace_back(actorNodes.first, n);
		}
	}
	// a toy: its line of balls (CollisionHub.cpp AddPropColliders, root to tip) is the tube's line. OCBPC
	// ADDED every overlapping ball's push, and a toy is ~16 balls 1.5 apart: a lip against it took two or
	// three pushes at once and rode as they passed, the ball-riding the penis tube cured (A-35)
	for (auto& c : otherColliders) {
		if (!propsToo || !c.isProp || !c.colliderActor || c.collisionSpheres.size() < 2)
			continue;
		Chain ch{ c.colliderActor, true, {}, NiPoint3(), NiPoint3() };
		float widest = 0.0f;
		for (auto& s : c.collisionSpheres) {
			float r = (std::max)(0.2f, (float)s.radius - skin);   // the same reading as a penis's
			ch.pts.push_back(Point{ s.worldPos, r });
			widest = (std::max)(widest, r);
		}
		ch.lo = ch.hi = ch.pts.front().pos;
		for (auto& p : ch.pts) {
			ch.lo = NiPoint3((std::min)(ch.lo.x, p.pos.x), (std::min)(ch.lo.y, p.pos.y), (std::min)(ch.lo.z, p.pos.z));
			ch.hi = NiPoint3((std::max)(ch.hi.x, p.pos.x), (std::max)(ch.hi.y, p.pos.y), (std::max)(ch.hi.z, p.pos.z));
		}
		ch.lo = ch.lo - NiPoint3(widest, widest, widest);
		ch.hi = ch.hi + NiPoint3(widest, widest, widest);
		tubes.push_back(ch);
		members.emplace_back(c.colliderActor, c.colliderNodeName);
	}
	Note("tube|built|" + std::to_string(tubes.size()), "[tube] %d tube(s) this frame (the first frame with this many)\n",
		(int)tubes.size());
}

bool IsTubeMember(const Actor* owner, const std::string& node)
{
	for (auto& m : members)
		if (m.first == owner && m.second == node)
			return true;
	return false;
}

bool TubePush(const Actor* self, const char* bone, const std::vector<Sphere>& spheres, NiPoint3& push)
{
	bool hit = false;
	for (auto& t : tubes) {
		if (!Tube::Reaches(t.prop, t.owner == self, t.prop && PropReaches(bone)))
			continue;
		for (auto& s : spheres) {
			float r = (float)s.radius;
			const NiPoint3& c = s.worldPos;
			if (c.x + r < t.lo.x || c.x - r > t.hi.x || c.y + r < t.lo.y || c.y - r > t.hi.y ||
				c.z + r < t.lo.z || c.z - r > t.hi.z)
				continue;
			NiPoint3 out;
			if (Tube::Push(t.pts, c, r, out)) {
				push = push + out;
				hit = true;
			}
		}
	}
	return hit;
}
