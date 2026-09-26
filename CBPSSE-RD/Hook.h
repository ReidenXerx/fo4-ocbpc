#pragma once
// fo4-anatomy: the Runtime Database build's hooks (Hook.cpp). Each installs only where its Address Library ids are
// proven for the running executable, and says in cbp.log what it did.

namespace Hook
{
	// (OG 1.10.163, AE 1.11.x) Address Library ids. OG from version-1-10-163-0.bin (tools/rd/addrlib.py); AE from the
	// matched function's AE address (tools/rd/xmatch.py) through version-1-11-240-0.bin; NG resolves through Runtime
	// Database's own record for the AE id. 0 = not proven for that family: whatever needs it stays off there.
	struct IdPair
	{
		std::uint64_t og;
		std::uint64_t ae;
	};

	[[nodiscard]] bool OgFamily();                                          // 1.10.163 (the classic build's game)
	[[nodiscard]] std::optional<std::uintptr_t> Resolve(const IdPair& a_id);   // an absolute address, or nothing
	// The one direct call to `target` inside `owner` (REL::resolve_callsites, then the E8/rel32 check), or nothing,
	// with a cbp.log line saying why, prefixed with `a_what`.
	[[nodiscard]] std::optional<std::uintptr_t> CallSite(const IdPair& a_owner, const IdPair& a_target, const char* a_what);
	// Points a call at `a_fn` through F4SE's trampoline; returns the function it called before.
	std::uintptr_t WriteCall(std::uintptr_t a_site, std::uintptr_t a_fn);

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
