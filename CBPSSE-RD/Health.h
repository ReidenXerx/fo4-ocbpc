// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-30: the health check (the owner's request via Watcher).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#pragma once
// Players report "no jiggle", "no genitals", "the penis points away" and each report costs a round of log requests.
// A few seconds after every save load the engine looks at what it can see itself and writes
// Documents\My Games\Fallout4\F4SE\Anatomy_Health.txt:
//   - the engine: its version, the runtime, the file it was loaded from, other physics plugins beside it;
//   - the physics preset in use (the player's ocbp.ini or Anatomy's), its whitelist and the bones it moves;
//   - the women's body (Data\Meshes\...\FemaleBody.nif): Anatomy's? which body (CBBE or 3BBB)? is it weighted to the
//     breast and butt bones the preset moves?
//   - AnatomyBuilder's stamp (F4SE\Plugins\Anatomy\build.ini): does the preset in use still fit what it built?
//   - the actors the physics runs on, the women's bodies and the men's penis chains (skeleton).
// A problem found is shown once per game session in a message box; a healthy game shows nothing.

namespace Health
{
	void OnGameLoaded();   // main.cpp, kPostLoadGame: check again in a few seconds
	void Tick();           // scan.cpp, every frame the physics runs (main thread)
}
