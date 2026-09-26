// fo4-anatomy: the hooks of the Runtime Database build. The classic build patched fixed 1.10.163 addresses; here every
// hook is a call site found the way Runtime Database validates it (REL::resolve_callsites: owner and target resolved
// by Address Library id, then the unique direct call to the target inside the owner's logical function), checked
// again the classic way (a direct E8 whose rel32 lands on the target), and only then patched. An id not proven for
// a runtime is 0 (Hook.h): the feature stays off there, with one line in cbp.log, never a guessed address.
//
// The per-frame hook: the classic build detoured the entry of 1.10.163's ProcessEventQueue_Internal (0x211CF80).
// That function has exactly one direct caller in 1.10.163 (the call at 0x20432C0, in 0x2042430; scanned 2026-09-26),
// so hooking that call is the same thing.
#include "PCH.h"
#include "Hook.h"

void UpdateActors();

namespace
{
	constexpr Hook::IdPair kFrameOwner{ 239710, 0 };    // 1.10.163 0x2042430: the only caller of the one below
	constexpr Hook::IdPair kFrameTarget{ 967097, 0 };   // 1.10.163 0x211CF80: ProcessEventQueue_Internal

	using Frame_t = void (*)(void*);
	Frame_t original = nullptr;

	void Frame(void* a_this)
	{
		original(a_this);
		UpdateActors();
	}

	[[nodiscard]] bool Known(const Hook::IdPair& a_id) { return Hook::OgFamily() ? a_id.og != 0 : a_id.ae != 0; }
	[[nodiscard]] REL::ID Id(const Hook::IdPair& a_id) { return REL::ID(a_id.og, a_id.ae); }
}

namespace Hook
{
	bool OgFamily() { return REL::Module::get().version() < REL::Version(1, 10, 980, 0); }

	std::optional<std::uintptr_t> Resolve(const IdPair& a_id)
	{
		if (!Known(a_id)) {
			return std::nullopt;
		}
		const auto result = REL::IDDatabase::get().resolve(Id(a_id));
		if (!result) {
			return std::nullopt;
		}
		return REL::Module::get().base() + *result.rva;
	}

	std::optional<std::uintptr_t> CallSite(const IdPair& a_owner, const IdPair& a_target, const char* a_what)
	{
		const auto version = REL::Module::get().version().string();
		if (!Known(a_owner) || !Known(a_target)) {
			rdlog::warn("{}: no proven id for Fallout 4 {} yet: off on this runtime", a_what, version);
			return std::nullopt;
		}
		const auto sites = REL::resolve_callsites(Id(a_owner), Id(a_target));
		if (sites.rvas.size() != 1) {
			rdlog::warn("{}: {} call site(s) on {} ({}): off", a_what, sites.rvas.size(), version,
				REL::id_resolve_status_text(sites.status));
			return std::nullopt;
		}
		const auto target = Resolve(a_target);
		const auto site = REL::Module::get().base() + sites.rvas[0];
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(site);
		if (!target || bytes[0] != 0xE8 || site + 5 + *reinterpret_cast<const std::int32_t*>(bytes + 1) != *target) {
			rdlog::warn("{}: the call at +{:X} is not a direct call to the target on {}: off", a_what, sites.rvas[0], version);
			return std::nullopt;
		}
		return site;
	}

	std::uintptr_t WriteCall(std::uintptr_t a_site, std::uintptr_t a_fn)
	{
		// ONCE: AllocTrampoline replaces the trampoline region on every call (CommonLibF4RD src/F4SE/API.cpp), which
		// would strand the stubs of the hooks written before. 1 KB holds this plugin's four call sites many times over.
		static const bool allocated = [] {
			F4SE::AllocTrampoline(1 << 10);
			return true;
		}();
		(void)allocated;
		return F4SE::GetTrampoline().write_call<5>(a_site, a_fn);
	}

	bool InstallFrame()
	{
		const auto site = CallSite(kFrameOwner, kFrameTarget, "per-frame hook (the physics)");
		if (!site) {
			return false;
		}
		original = reinterpret_cast<Frame_t>(WriteCall(*site, reinterpret_cast<std::uintptr_t>(&Frame)));
		rdlog::info("per-frame hook: installed at +{:X} on Fallout 4 {}", *site - REL::Module::get().base(),
			REL::Module::get().version().string());
		return true;
	}
}
