// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-25: where a glance puts the eyes (RFAG, Eyes.h).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// Fallout 4 turns an eye by sliding its TEXTURE: the eye mesh's shader material carries a UV offset
// (BSShaderMaterial::textCoordOffset, +0x0C and +0x14), and the engine's eye update (1.10.163 +0x9C0410,
// every frame, called from +0xD38B13 and +0xD39681) eases every tracked actor's offset toward a target at
// 2.0 a second. Read out of the GOG executable (2026-09-25), the target (+0x9BE800) is 0.25 x two parts of
// the look direction: the FIRST (u) from its vertical part, negated, clamped to +-0.7; the SECOND (v) from its
// sideways part, clamped to -0.34 .. 0.27. Past |u| 0.16 or v outside -0.08 .. 0.07 the engine gives up and
// looks straight ahead. Screen Archer Menu sets the same offset to pose eyes (SAF eyes.cpp).
//
// The first build had them the other way round (u sideways): the owner saw the eyes go a quarter-turn off
// ("his head on perfect 12 h then her gaze is somewhere 8-10 h"), and the probe showed the engine itself
// turning v, not u, toward a player beside the NPC (side +0.44 .. +0.9 -> v -0.066 .. -0.075, u ~0).
// So: u = -0.25 x up, v = -0.25 x side ([Eyes] signUp / signSide). Free of F4SE: tests/face runs it.
#include <cmath>

namespace GlanceMath
{
	struct Params
	{
		float gain = 0.25f;          // UV per unit of the direction's up / sideways part (the engine's)
		float xMax = 0.15f;          // u (vertical): inside the engine's own give-up edges (|u| 0.16,
		float yMin = -0.075f, yMax = 0.065f;   // v (sideways): -0.08 .. 0.07)
		float signUp = -1.0f, signSide = -1.0f;   // the engine's: u = -0.25 up, v = -0.25 side
		float minAhead = 0.3f;       // the target this far in front at least (a unit direction's forward
		                             // part): behind her, no eye can reach it and the glance does nothing
		float speed = 2.0f;          // UV a second (the engine's)
	};
	struct UV
	{
		float x = 0.0f, y = 0.0f;
	};

	// side, up, ahead: the unit direction from her eyes to his, in her head's frame. False: out of reach.
	// out.x is the material's u (vertical), out.y its v (sideways).
	inline bool Want(float side, float up, float ahead, const Params& p, UV& out)
	{
		if (!(ahead >= p.minAhead))
			return false;
		float x = p.signUp * p.gain * up, y = p.signSide * p.gain * side;
		out.x = x < -p.xMax ? -p.xMax : (x > p.xMax ? p.xMax : x);
		out.y = y < p.yMin ? p.yMin : (y > p.yMax ? p.yMax : y);
		return true;
	}

	// from where the eye is toward where it is wanted, at most speed x dt of the way
	inline UV Step(UV cur, UV want, const Params& p, float dt)
	{
		float dx = want.x - cur.x, dy = want.y - cur.y;
		float d = std::sqrt(dx * dx + dy * dy), most = p.speed * dt;
		if (d <= most || d < 1e-6f)
			return want;
		return UV{ cur.x + dx * most / d, cur.y + dy * most / d };
	}
}
