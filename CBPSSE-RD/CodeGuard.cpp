// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-13.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "PCH.h"
#include "CodeGuard.h"

#include <windows.h>

namespace
{
	std::uintptr_t moduleBase = 0;    // for the log's offsets, comparable with the release's symbols
	std::uint8_t* code = nullptr;     // this DLL's code section, as mapped
	std::size_t codeSize = 0;
	std::uint8_t* pristine = nullptr; // its copy, taken in DllMain: relocated, before any plugin's Load
	std::mutex lock;
	std::string undone;               // the health check's line: every run put back, in order
	unsigned frames = 0;
	unsigned logged = 0;              // a plugin that writes again every frame gets 10 lines, not a full log

	void Snapshot(HMODULE a_self)
	{
		auto* base = reinterpret_cast<std::uint8_t*>(a_self);
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const auto* sec = IMAGE_FIRST_SECTION(nt);
		for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
			if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) || std::memcmp(sec->Name, ".text", 6) != 0)
				continue;
			// HeapAlloc, not new: DllMain holds the loader lock, and this copy lives as long as the process
			auto* copy = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, sec->Misc.VirtualSize));
			if (!copy)
				return;
			std::memcpy(copy, base + sec->VirtualAddress, sec->Misc.VirtualSize);
			moduleBase = reinterpret_cast<std::uintptr_t>(base);
			code = base + sec->VirtualAddress;
			codeSize = sec->Misc.VirtualSize;
			pristine = copy;
			return;
		}
	}
}

BOOL WINAPI DllMain(HINSTANCE a_self, DWORD a_reason, LPVOID)
{
	if (a_reason == DLL_PROCESS_ATTACH)
		Snapshot(a_self);
	return TRUE;
}

namespace CodeGuard
{
	void Check(const char* a_when)
	{
		if (!pristine)
			return;
		// per frame: every 60th; a plugin's patch lands at load time, the scan costs ~0.1 ms
		if (std::strcmp(a_when, "frame") == 0 && frames++ % 60 != 0)
			return;
		if (std::memcmp(code, pristine, codeSize) == 0)
			return;
		std::lock_guard guard(lock);
		for (std::size_t i = 0; i < codeSize;) {
			if (code[i] == pristine[i]) {
				++i;
				continue;
			}
			std::size_t end = i;
			while (end < codeSize && code[end] != pristine[end])
				++end;
			const std::size_t len = end - i;
			DWORD old = 0;
			if (!VirtualProtect(code + i, len, PAGE_EXECUTE_READWRITE, &old)) {
				rdlog::error("[guard] another plugin wrote {} bytes into cbp.dll at +{:X} ({}), and they could not be put "
				             "back", len, reinterpret_cast<std::uintptr_t>(code + i) - moduleBase, a_when);
				return;
			}
			std::memcpy(code + i, pristine + i, len);
			VirtualProtect(code + i, len, old, &old);
			FlushInstructionCache(GetCurrentProcess(), code + i, len);
			const auto at = reinterpret_cast<std::uintptr_t>(code + i) - moduleBase;
			const bool hfpf = GetModuleHandleW(L"HighFPSPhysicsFix.dll") != nullptr;
			if (logged++ < 10)
				rdlog::warn("[guard] another plugin wrote {} bytes into cbp.dll at +{:X} (seen at {}): put back.{}", len, at,
				a_when, hfpf ? " High FPS Physics Fix is loaded: its FixOCBPSpeed patches the original OCBP's cbp.dll at "
				                "+18A18; set FixOCBPSpeed=false in HighFPSPhysicsFix.ini" : "");
			if (undone.size() < 400)
				undone += std::format("{}{} bytes at +{:X}", undone.empty() ? "" : ", ", len, at);
			i = end;
		}
	}

	std::string Undone()
	{
		std::lock_guard guard(lock);
		return undone;
	}
}
