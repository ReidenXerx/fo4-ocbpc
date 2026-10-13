// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-10-13.
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// Other plugins that know the ORIGINAL OCBP patch "cbp.dll" at fixed offsets, and this engine keeps that file name.
// High FPS Physics Fix's FixOCBPSpeed writes a 16-byte jump at cbp.dll+0x18A18: in this engine that is the middle of
// the penis aim, so the first frame a scene aimed, the game ran into it and closed with no crash log (zenar45 via
// Watcher, 2026-10-13, OG 1.10.163: ProcDump's dump showed our code differing from the release in exactly those bytes).
// The code section is copied when the DLL is mapped, before any plugin's Load runs; whatever differs later is put back.

#include <string>

namespace CodeGuard
{
	void Check(const char* a_when);   // main.cpp's F4SE messages, and Hook.cpp's per-frame hook (every 60th frame)
	std::string Undone();             // for the health check: what was put back, and who is known to write there
}
