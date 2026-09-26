// fo4-anatomy: the per-frame hook on Runtime Database. The classic build detoured the entry of 1.10.163's
// ProcessEventQueue_Internal (0x211CF80) with DetourXS; that function has exactly one direct caller in 1.10.163 (a call
// at 0x20432C0 inside 0x2042430, scanned 2026-09-26), so hooking that one call is the same thing, and it is the form
// Runtime Database validates: the owner and the target resolved by Address Library id, then the unique direct call to
// the target found inside the owner (REL::resolve_callsites). An id not proven for a runtime is 0 here: the hook, and
// the physics with it, stays off there, with one line in cbp.log, never a guessed address.
#include "PCH.h"
#include "Hook.h"

void UpdateActors();

namespace
{
	// (OG 1.10.163, AE 1.11.x) Address Library ids. OG from version-1-10-163-0.bin (tools/rd/addrlib.py); AE from
	// the matched function's AE address (tools/rd/xmatch.py) through version-1-11-240-0.bin. NG resolves through
	// Runtime Database's own record for the AE id.
	struct IdPair
	{
		std::uint64_t og;
		std::uint64_t ae;
	};
	constexpr IdPair kFrameOwner{ 239710, 0 };    // 1.10.163 0x2042430: the only caller of the one below
	constexpr IdPair kFrameTarget{ 967097, 0 };   // 1.10.163 0x211CF80: ProcessEventQueue_Internal

	using Frame_t = void (*)(void*);
	Frame_t original = nullptr;

	void Frame(void* a_this)
	{
		original(a_this);
		UpdateActors();
	}

	[[nodiscard]] bool Known(const IdPair& a_id)
	{
		const auto family = REL::Module::get().version() < REL::Version(1, 10, 980, 0) ? 0 : 1;
		return family == 0 ? a_id.og != 0 : a_id.ae != 0;
	}

	[[nodiscard]] REL::ID Id(const IdPair& a_id) { return REL::ID(a_id.og, a_id.ae); }
}

namespace Hook
{
	bool InstallFrame()
	{
		const auto version = REL::Module::get().version().string();
		if (!Known(kFrameOwner) || !Known(kFrameTarget)) {
			rdlog::warn("per-frame hook: no proven id for Fallout 4 {} yet: physics OFF on this runtime", version);
			return false;
		}
		const auto sites = REL::resolve_callsites(Id(kFrameOwner), Id(kFrameTarget));
		if (sites.rvas.size() != 1) {
			rdlog::warn("per-frame hook: {} call site(s) found on {} ({}): physics OFF", sites.rvas.size(), version,
				REL::id_resolve_status_text(sites.status));
			return false;
		}
		const auto site = REL::Module::get().base() + sites.rvas[0];
		const auto target = REL::IDDatabase::get().resolve(Id(kFrameTarget));
		// the classic engine's own check, again: a direct E8 call whose rel32 lands on the target
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(site);
		const auto lands = site + 5 + *reinterpret_cast<const std::int32_t*>(bytes + 1);
		if (bytes[0] != 0xE8 || !target || lands != REL::Module::get().base() + *target.rva) {
			rdlog::warn("per-frame hook: the call at +{:X} is not a direct call to the target on {}: physics OFF",
				sites.rvas[0], version);
			return false;
		}
		F4SE::AllocTrampoline(1 << 7);
		auto& trampoline = F4SE::GetTrampoline();
		original = reinterpret_cast<Frame_t>(trampoline.write_call<5>(site, reinterpret_cast<std::uintptr_t>(&Frame)));
		rdlog::info("per-frame hook: installed at +{:X} on Fallout 4 {}", sites.rvas[0], version);
		return true;
	}
}
