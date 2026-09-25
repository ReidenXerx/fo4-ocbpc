// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-25: where a glance puts the eyes (RFAG, Eyes.h).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// Fallout 4 turns an eye by sliding its TEXTURE: the eye mesh's shader material carries a UV offset
// (BSShaderMaterial::textCoordOffset, +0x0C and +0x14), and the engine's eye update (1.10.163 +0x9C0410,
// every frame, called from +0xD38B13 and +0xD39681) eases every tracked actor's offset toward a target at
// 2.0 a second. Read out of the GOG executable (2026-09-25), the target is the look direction's sideways
// and up parts in the head's frame, clamped to 0.7 and -0.34 .. 0.27, times 0.25; past |x| 0.16 or
// y outside -0.08 .. 0.07 the engine gives up and looks straight ahead. Screen Archer Menu sets the same
// offset to pose eyes (SAF eyes.cpp).
//
// So a glance is the same sum, toward the partner's eyes, written after the engine's own each frame. The
// signs (which way +x turns the eye) are the probe's to measure ([Eyes] signX/signY); the rest is the
// engine's own numbers. Free of F4SE: tests/lips runs it.
#include <cmath>

namespace GlanceMath
{
	struct Params
	{
		float gain = 0.25f;          // UV per unit of the direction's sideways / up part (the engine's)
		float xMax = 0.15f;          // inside the engine's own give-up edges (0.16, -0.08 .. 0.07)
		float yMin = -0.075f, yMax = 0.065f;
		float signX = 1.0f, signY = 1.0f;
		float minAhead = 0.3f;       // the target this far in front at least (a unit direction's forward
		                             // part): behind her, no eye can reach it and the glance does nothing
		float speed = 2.0f;          // UV a second (the engine's)
	};
	struct UV
	{
		float x = 0.0f, y = 0.0f;
	};

	// side, up, ahead: the unit direction from her eyes to his, in her head's frame. False: out of reach.
	inline bool Want(float side, float up, float ahead, const Params& p, UV& out)
	{
		if (!(ahead >= p.minAhead))
			return false;
		float x = p.signX * p.gain * side, y = p.signY * p.gain * up;
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
