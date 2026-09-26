#pragma once
// fo4-anatomy: the Runtime Database build's hooks (Hook.cpp). Each installs only where its Address Library ids are
// proven for the running executable, and says in cbp.log what it did.

namespace Hook
{
	bool InstallFrame();   // UpdateActors once per frame (the classic ProcessEventQueue_Internal detour)
}

namespace Layout
{
	// The members CommonLibF4RD does not define (Game.h, G::Measured), checked on the running executable before the
	// bones and the collisions trust them: a body's skin instance, read once under SEH, must hold real nodes in its
	// bone slots and, in its transform slots, pointers to exactly those nodes' world transforms. Unchecked until the
	// first skinned body is seen; a mismatch or a fault turns the skin work off for the session (never half on).
	enum class State
	{
		kUnchecked,
		kGood,
		kBad
	};
	State Skin();
	void CheckSkin(RE::BSGeometry* a_geometry);
}
