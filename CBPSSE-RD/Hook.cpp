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
	// 1.10.163 0x211CF80 is a 4-instruction reset (xor eax,eax; mov [rcx+0Ch],rax; mov [rcx+4],rax; ret) whose one
	// direct caller runs once per frame. Its bytes are unique in both executables (tools/rd/bytecallers.py), with one
	// caller each: AE 1.11.240 0x1B1E2F0, called once at 0x1A815BC in 0x1A80610. AE ids from f4rd-runtime.bin and the
	// AE Address Library agree; both records also know NG 1.10.984.
	constexpr Hook::IdPair kFrameOwner{ 239710, 2284754 };    // 0x2042430 / AE 0x1A80610: the only caller of the one below
	constexpr Hook::IdPair kFrameTarget{ 967097, 2287625 };   // 0x211CF80 / AE 0x1B1E2F0: the classic "ProcessEventQueue_Internal"

	// The reset's own 11 bytes (xor eax,eax; mov [rcx+0Ch],rax; mov [rcx+4],rax; ret), the same in 1.10.163 and 1.11.240:
	// checked before its entry is patched, and done again here, whole, before the physics.
	constexpr std::uint8_t kReset[11] = { 0x33, 0xC0, 0x48, 0x89, 0x41, 0x0C, 0x48, 0x89, 0x41, 0x04, 0xC3 };

	std::uint64_t Frame(void* a_this)
	{
		*reinterpret_cast<std::uint64_t*>(static_cast<char*>(a_this) + 0x0C) = 0;
		*reinterpret_cast<std::uint64_t*>(static_cast<char*>(a_this) + 0x04) = 0;
		UpdateActors();
		return 0;   // the reset's xor eax,eax
	}

	// ONCE: AllocTrampoline replaces the trampoline region on every call (CommonLibF4RD src/F4SE/API.cpp), which would
	// strand the stubs of the hooks written before. 1 KB holds this plugin's hooks many times over.
	void EnsureTrampoline()
	{
		static const bool allocated = [] {
			F4SE::AllocTrampoline(1 << 10);
			return true;
		}();
		(void)allocated;
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
		EnsureTrampoline();
		return F4SE::GetTrampoline().write_call<5>(a_site, a_fn);
	}

	// On the reset's ENTRY, as the classic build detoured it - not on its one call: F4SE hooks that call itself (its task
	// queue: measured in the running AE game 2026-09-26, the call at +1A815BC led into f4se_1_11_240.dll+0x16310, and our
	// call hook installed before it was silently replaced, so the physics never ran). F4SE's hook still ends in the
	// reset, so the entry is reached every frame whoever owns the call. The owner's call is still resolved first, to
	// prove the target is the reset this build was matched on.
	bool InstallFrame()
	{
		const auto version = REL::Module::get().version().string();
		const auto target = Resolve(kFrameTarget);
		if (!target || !CallSite(kFrameOwner, kFrameTarget, "per-frame hook (the physics)")) {
			if (!target)
				rdlog::warn("per-frame hook: the reset does not resolve on {}: physics OFF", version);
			return false;
		}
		if (std::memcmp(reinterpret_cast<const void*>(*target), kReset, sizeof(kReset)) != 0) {
			rdlog::warn("per-frame hook: the reset's bytes are not the ones matched on {}: physics OFF", version);
			return false;
		}
		EnsureTrampoline();
		F4SE::GetTrampoline().write_branch<5>(*target, reinterpret_cast<std::uintptr_t>(&Frame));
		rdlog::info("per-frame hook: installed on the reset's entry at +{:X} on Fallout 4 {}", *target - REL::Module::get().base(),
			version);
		return true;
	}
}
