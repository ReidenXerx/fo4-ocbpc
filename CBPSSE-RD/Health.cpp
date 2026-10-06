// fo4-ocbpc: written for fo4-anatomy by ReidenXerx, 2026-09-30: the health check (Health.h).
// Licensed under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "Health.h"

#include "ActorEntry.h"
#include "ActorUtils.h"
#include "Aim.h"
#include "Sound.h"
#include "Bones.h"
#include "Game.h"
#include "Hook.h"
#include "INIReader.h"
#include "SimObj.h"
#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
	using Clock = std::chrono::steady_clock;
	constexpr auto kWait = std::chrono::seconds(10);   // after the load: the actors near the player have their 3D
	constexpr int kMinFrames = 120;                    // and the physics has run a while
	const char* kFemaleBody = "Data\\Meshes\\Actors\\Character\\CharacterAssets\\FemaleBody.nif";
	const char* kPlayerPreset = "Data\\F4SE\\Plugins\\ocbp.ini";
	const char* kStamp = "Data\\F4SE\\Plugins\\Anatomy\\build.ini";
	const char* kPlugins = "Data\\F4SE\\Plugins";
	const char* kFix = "Re-run AnatomyBuilder, then build \"Anatomy Body\" in BodySlide.";

	bool armed = false;
	Clock::time_point loadedAt;
	int frames = 0;
	bool shown = false;   // the message box: once per game session

	std::string Lower(std::string s)
	{
		for (auto& c : s)
			c = (char)tolower((unsigned char)c);
		return s;
	}

	bool ReadFile(const std::filesystem::path& path, std::string& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;
		std::ostringstream ss;
		ss << f.rdbuf();
		out = ss.str();
		return true;
	}

	// A NIF's header strings are a 4-byte length and the characters: the exact name, not a longer one holding it
	// (LBreast_01_skin inside LBreast_01_skin_x). Names compare without case, as the game's lookups do.
	bool NifHasName(const std::string& nif, const std::string& nifLower, const std::string& name)
	{
		const std::string want = Lower(name);
		const std::uint32_t n = (std::uint32_t)want.size();
		for (size_t at = nifLower.find(want); at != std::string::npos; at = nifLower.find(want, at + 1)) {
			std::uint32_t len = 0;
			if (at >= 4)
				memcpy(&len, nif.data() + at - 4, 4);
			if (len == n)
				return true;
		}
		return false;
	}

	std::uint32_t Fnv1a(const std::string& bytes)   // tools/builder.py stamps the same hash
	{
		std::uint32_t h = 2166136261u;
		for (unsigned char c : bytes) {
			h ^= c;
			h *= 16777619u;
		}
		return h;
	}

	std::string Hex8(std::uint32_t v)
	{
		char b[16];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "%08x", v);
		return b;
	}

	std::string Form(std::uint32_t id)
	{
		char b[16];
		_snprintf_s(b, sizeof(b), _TRUNCATE, "%08X", id);
		return b;
	}

	std::string Join(const std::vector<std::string>& v, const char* sep = ", ")
	{
		std::string s;
		for (auto& x : v)
			s += (s.empty() ? "" : sep) + x;
		return s.empty() ? "-" : s;
	}

	struct Report
	{
		std::ostringstream text;
		std::vector<std::string> problems;   // one line each, for the message box
		void Problem(const std::string& line, const std::string& detail = {})
		{
			problems.push_back(line);
			text << "  PROBLEM: " << line << "\n";
			if (!detail.empty())
				text << "           " << detail << "\n";
		}
	};

	// the bones the preset moves that are not Anatomy's own genital bones (those come from Anatomy's ini)
	std::vector<std::string> PresetBones()
	{
		std::vector<std::string> out;
		std::set<std::string> seen;
		for (auto& b : boneNames)
			if (!anatomyBones.count(b) && seen.insert(Lower(b)).second)
				out.push_back(b);
		return out;
	}

	std::vector<std::string> Having(const std::vector<std::string>& bones, const char* word)
	{
		std::vector<std::string> out;
		for (auto& b : bones)
			if (Lower(b).find(word) != std::string::npos)
				out.push_back(b);
		return out;
	}

	bool Attaches(const std::vector<std::string>& bones, const char* name)
	{
		for (auto& b : bones)
			if (_stricmp(b.c_str(), name) == 0)
				return true;
		return false;
	}

	void Engine(Report& r)
	{
		std::string runtime = REL::Module::get().version().string();
		std::replace(runtime.begin(), runtime.end(), '-', '.');
		r.text << "Engine: Anatomy Engine " << OCBPC_VERSION_STRING << " (Runtime Database) on Fallout 4 " << runtime << "\n";
		std::wstring self;
		HMODULE mod = nullptr;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&Health::Tick), &mod)) {
			wchar_t buf[MAX_PATH] = {};
			GetModuleFileNameW(mod, buf, MAX_PATH);
			self = buf;
		}
		std::error_code ec;
		const std::filesystem::path selfPath(self);
		r.text << "  loaded from: " << selfPath.string() << " (" << std::filesystem::file_size(selfPath, ec) << " bytes)\n";
		// another physics engine beside ours: two of them move the same bones
		std::vector<std::string> others;
		for (auto it = std::filesystem::directory_iterator(kPlugins, ec); !ec && it != std::filesystem::directory_iterator();
				it.increment(ec)) {
			const auto name = it->path().filename().string();
			const auto low = Lower(name);
			if (low.size() > 4 && low.substr(low.size() - 4) == ".dll" && low.find("cbp") != std::string::npos &&
					_wcsicmp(it->path().filename().c_str(), selfPath.filename().c_str()) != 0)
				others.push_back(name);
		}
		r.text << "  other physics plugins in Data\\F4SE\\Plugins: " << Join(others) << "\n";
		if (!others.empty())
			r.Problem("Another physics plugin is installed beside Anatomy Engine (" + Join(others) + "). Two engines move the "
				"same bones and fight: keep only Anatomy Engine's cbp.dll.");
	}

	void Preset(Report& r, const std::vector<std::string>& bones)
	{
		bool anatomys = false;
		const std::string& path = PresetInUse(anatomys);
		r.text << "\nPhysics preset: " << path << (anatomys ? " (Anatomy's: you have no ocbp.ini of your own)" : " (yours)") << "\n";
		r.text << "  whitelist: " << (useWhitelist ? "on, races " + Join(raceWhitelist) : std::string("off")) << "\n";
		r.text << "  only: " << (playerOnly ? "player " : "") << (npcOnly ? "NPCs " : "") << (femaleOnly ? "women " : "")
		       << (maleOnly ? "men " : "") << (!playerOnly && !npcOnly && !femaleOnly && !maleOnly ? "everyone" : "") << "\n";
		r.text << "  bones it moves: " << Join(bones) << "\n";
		r.text << "  Anatomy's genital bones: " << anatomyBones.size() << "\n";
		if (bones.empty())
			r.Problem("Your physics preset moves no body bones: breasts and butt stay still.",
				"Its [Attach] section is empty or missing: " + path);
		// a preset of the player's own that is not one of Anatomy's (a player, 2026-10-06: 3BBB's own ocbp.ini, and in
		// scenes the vulva stayed closed while the penis slid along it). Anatomy's presets open with a fixed header line,
		// so a collection that ships one at the player's path (Ivy) still reads as Anatomy's
		if (!anatomys) {
			std::string head;
			if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
				char buf[256] = {};
				const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
				std::fclose(f);
				head.assign(buf, n);
			}
			if (head.find("Anatomy's default physics preset") == std::string::npos) {
				r.text << "  not one of Anatomy's presets (its first line: " << head.substr(0, head.find_first_of("\r\n")) << ")\n";
				r.Problem("Your physics preset is not Anatomy's (" + path + "), and another body physics preset is "
					"incompatible with Anatomy.",
					"It replaces the preset AnatomyBuilder made for your body (Anatomy\\ocbp-body.ini): breasts move by "
					"another body's tuning and in scenes the genitals stay closed. Remove or hide it (in MO2: right-click "
					"F4SE\\Plugins\\ocbp.ini and OCBPCollisionConfig.txt in the mod that ships them, often 3BBB, and Hide), "
					"then run AnatomyBuilder again.");
			}
		}
	}

	// the women's body on disk: Anatomy's? which body? weighted to what the preset moves?
	void Body(Report& r, const std::vector<std::string>& bones, std::string& kind)
	{
		r.text << "\nWomen's body: " << kFemaleBody << "\n";
		std::string nif;
		if (!ReadFile(kFemaleBody, nif)) {
			r.text << "  not there as a loose file: women wear the one inside an archive (the game's or a body mod's)\n";
			r.Problem("Anatomy's body is not built: there is no FemaleBody.nif in Data.", std::string("In BodySlide, choose the "
				"set \"Anatomy Body\" and your preset, and Build (into your game's Data, or your BodySlide output mod)."));
			return;
		}
		const std::string low = Lower(nif);
		const bool anatomy = NifHasName(nif, low, "AnatomyGenitals");
		const bool tbbb = NifHasName(nif, low, "LBreast_01_skin");
		kind = tbbb ? "3bbb" : "cbbe";
		r.text << "  " << nif.size() << " bytes; " << (anatomy ? "Anatomy's (has AnatomyGenitals)" : "NOT Anatomy's (no AnatomyGenitals)")
		       << "; " << (tbbb ? "a 3BBB body (LBreast_01_skin)" : "a CBBE body (no 3BBB breast bones)") << "\n";
		if (!anatomy)
			r.Problem("The women's body is not Anatomy's (FemaleBody.nif has no genitals).", "Build \"Anatomy Body\" in BodySlide, "
				"and make sure no other body mod or older BodySlide output overwrites that FemaleBody.nif.");
		const char* parts[][2] = { { "breast", "breasts" }, { "butt", "butt" } };
		for (auto& part : parts) {
			const auto moved = Having(bones, part[0]);
			if (moved.empty())
				continue;
			std::vector<std::string> weighted;
			for (auto& b : moved)
				if (NifHasName(nif, low, b))
					weighted.push_back(b);
			r.text << "  " << part[1] << ": the preset moves " << Join(moved) << "; the body is weighted to " << Join(weighted) << "\n";
			if (weighted.empty())
				r.Problem(std::string("Your physics preset moves the ") + part[1] + " with bones your body is not weighted to, "
					"so they stay still.", "The preset is made for another body (3BBB against CBBE, or the reverse): use a "
					"preset for your body, or " + std::string(kFix));
		}
	}

	// AnatomyBuilder's stamp: the body it built and how the breasts were weighted, against the preset running now
	void Builder(Report& r, const std::vector<std::string>& bones, const std::string& kind)
	{
		r.text << "\nAnatomyBuilder: " << kStamp << "\n";
		INIReader stamp(kStamp);
		if (stamp.ParseError() < 0) {
			r.text << "  no stamp (a builder older than this engine, or not run): not checked\n";
			return;
		}
		const std::string body = stamp.Get("Build", "body", "?"), breasts = stamp.Get("Build", "breasts", "?"),
		                  hash = stamp.Get("Build", "presetHash", "?");
		std::string preset;
		const std::string now = ReadFile(kPlayerPreset, preset) ? Hex8(Fnv1a(preset)) : "none";
		r.text << "  built " << stamp.Get("Build", "date", "?") << ": body " << body << ", breasts " << breasts
		       << "; your ocbp.ini then " << hash << ", now " << now << (hash == now ? " (unchanged)" : " (CHANGED since)") << "\n";
		if (!kind.empty() && body != kind)
			r.Problem("The builder made a " + body + " body, but the FemaleBody.nif in Data is " + kind + ".",
				"BodySlide's last build of the women's body was not \"Anatomy Body\" from this builder run. " + std::string(kFix));
		const bool drivesCbbe = Attaches(bones, "LBreast_skin") && Attaches(bones, "RBreast_skin");
		const bool drives3bbb = Attaches(bones, "LBreast_01_skin") && Attaches(bones, "RBreast_01_skin");
		std::string wrong;
		if (breasts == "3bbb" && !drives3bbb)
			wrong = "the body is 3BBB, but the preset running now does not move LBreast_01_skin/RBreast_01_skin";
		else if (breasts == "moved" && !drivesCbbe)
			wrong = "the breasts were weighted onto LBreast_skin/RBreast_skin, which the preset running now does not move";
		else if (breasts == "cloth" && drivesCbbe)
			wrong = "the preset running now moves LBreast_skin/RBreast_skin, but the breasts were left on CBBE's own bones";
		if (!wrong.empty())
			r.Problem("Your physics preset does not fit the body AnatomyBuilder built: " + wrong + ".",
				std::string("The preset changed after the builder ran. ") + kFix);
	}

	void Actors(Report& r)
	{
		r.text << "\nActors the physics runs on: " << actorEntries.size() << "\n";
		Actor* player = G::LookupActor(0x14);
		bool playerIn = false;
		std::vector<Actor*> seen;
		for (auto& e : actorEntries) {
			if (e.actor && std::find(seen.begin(), seen.end(), e.actor) == seen.end())
				seen.push_back(e.actor);
			playerIn = playerIn || e.id == 0x14;
		}
		if (player && !playerIn)
			seen.insert(seen.begin(), player);
		const int aim = AimChainState(nullptr);
		int men = 0, menChained = 0, menBroken = 0;
		for (Actor* a : seen) {
			const bool male = actorUtils::IsActorMale(a);
			const bool simulated = a != player || playerIn;
			r.text << "  " << Form(a->formID) << " " << G::RefName(a) << " (" << (male ? "man" : "woman")
			       << (simulated ? "" : ", NOT simulated") << ", race " << actorUtils::GetActorRaceEID(a) << "): ";
			if (male) {
				const int s = AimChainState(a);
				// only a man on the human skeleton says anything about ZeX's: a robot, a dog or a feral ghoul never has
				// penis bones (a player's report, 2026-10-02: Protectron, Ravager, Bee Swarm, Codsworth, Dog blamed ZeX)
				std::string race = actorUtils::GetActorRaceEID(a);
				std::transform(race.begin(), race.end(), race.begin(), [](unsigned char c) { return (char)tolower(c); });
				const bool human = race.find("human") != std::string::npos ||
				                   (race.find("ghoul") != std::string::npos && race.find("feral") == std::string::npos);
				if (!human) {
					r.text << "not a human skeleton: not counted\n";
					continue;
				}
				men += simulated;
				menChained += simulated && s >= 2;
				menBroken += simulated && s == 1;
				r.text << (s < 0 ? "no chain configured" : s == 0 ? "penis chain: bones missing from the skeleton" :
				           s == 1 ? "penis chain: bones do not hang one from the next" :
				           s == 2 ? "penis chain ok" : "penis chain ok (nodes between its bones, folded in)") << "\n";
				continue;
			}
			BodyView body;
			if (!DescribeBody(a, body)) {
				r.text << "could not read the body\n";
				continue;
			}
			const bool genitals = std::find(body.shapes.begin(), body.shapes.end(), "AnatomyGenitals") != body.shapes.end();
			r.text << (genitals ? "Anatomy's genitals" : "no Anatomy genitals (clothed, or another body)") << ", " << body.ours
			       << " of our bones in the skin; shapes " << Join(body.shapes) << "\n";
			// body bones the skin found outside her skeleton (a Havok cloth bone of a garment is not a body bone: only
			// *_skin names count)
			std::vector<std::string> nailed;
			for (const auto& bn : body.loose)
				if (bn.size() > 5 && _stricmp(bn.c_str() + bn.size() - 5, "_skin") == 0)
					nailed.push_back(bn);
			if (!nailed.empty()) {
				// which skeleton her race sends her to, and which plugin last edited that race (a player's case,
				// 2026-10-05: LooksMenu Customization Compendium and Pip-Boy 2000 overrode HumanRace after
				// DiscreteFemaleSkeleton.esp and sent women back to the base skeleton; Vortex shows no file conflict)
				std::string skel = "?", owner = "?", raceName = actorUtils::GetActorRaceEID(a);
				if (a->race) {
					const char* m = a->race->skeletonModel[1].model.c_str();
					skel = m && *m ? m : "(none)";
					if (RE::TESFile* f = a->race->GetFile(-1))
						owner = std::string(f->GetFilename());
				}
				r.text << "    her skeleton lacks " << Join(nailed) << ": the skin is bound to loose copies\n"
				       << "    her race " << raceName << " sends women to " << skel << " (last edited by " << owner << ")\n";
				// the race already names the women's skeleton: then the FILE there lacks the bones (another skeleton
				// mod wins over Skeletal Adjustments), and the race's last editor is not the one to blame
				std::string low = skel;
				for (auto& c : low)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				const bool raceOk = low.find("female\\skeleton.nif") != std::string::npos ||
				                    low.find("female/skeleton.nif") != std::string::npos;
				const std::string fix = raceOk ?
					"Her race " + raceName + " already sends women to " + skel + ", so that file lacks the bones: "
					"another skeleton mod overwrites Skeletal Adjustments for CBBE's female\\skeleton.nif. Make Skeletal "
					"Adjustments for CBBE (3BBB) win that file." :
					"Her race " + raceName + " sends women to " + skel + ", as last edited by " + owner + ". It must be "
					"Actors\\Character\\CharacterAssets\\female\\skeleton.nif (DiscreteFemaleSkeleton.esp sets it, and the "
					"file comes from Skeletal Adjustments for CBBE): load DiscreteFemaleSkeleton.esp after " + owner +
					", or make a patch that keeps that path.";
				r.Problem(G::RefName(a) + "'s skeleton lacks bones her body is weighted to (" + Join(nailed) + ").",
					"Those parts stay nailed in place while she moves (breasts fixed at one height, worst when she sits). " + fix);
			}
		}
		if (player && !playerIn && !actorUtils::IsActorMale(player))
			r.Problem("The physics skips your character.", "The preset's [General] (playerOnly, npcOnly, maleOnly) or its race "
				"whitelist (useWhitelist) leaves her out.");
		if (aim >= 0 && men > 0 && menChained == 0)
			r.Problem(menBroken ? "The men's penis bones do not hang one from the next: the penis is not aimed." :
				"The men's skeleton has no penis bones: Anatomy cannot aim or shape the penis.",
				"Make ZeX's skeleton.nif (Meshes\\Actors\\Character\\CharacterAssets\\skeleton.nif) win over other skeleton mods.");
		r.text << "  aim: " << (AimOn() ? "on" : "off") << ", chain " << AimChainText() << "; human men simulated " << men
		       << ", with a working chain " << menChained << (men == 0 ? " (no human man loaded: the men's skeleton is "
		       "not checked)" : "") << "\n";
		// the sex sounds (A-67): the mute, the engine's own audio calls, Rapport's override and what it muted
		r.text << "\nSex sounds: the packs' SoundPlay mute " << (Sound::Hooked() ? "hooked" : "NOT hooked (this build)")
		       << "; the engine's audio calls " << (Sound::CanPlay() ? "ready" : "not resolved") << "; override "
		       << (Sound::Override() ? "ON" : "off") << " (set by " << Sound::OverrideSource()
		       << "); Rapport " << (Sound::RapportLoaded() ? "loaded (the packs' voices are muted too: Rapport voices both "
		          "partners)" : "not loaded (only the packs' body sounds are muted: their voices play)")
		       << "; pack sounds muted so far " << Sound::MutedCount() << ", the engine's own played "
		       << Sound::PlayedCount() << ", Rapport's voices played " << Sound::VoicedCount() << "\n";
	}

	// the game's message box (MessageMenuManager::Create), resolved safely: nothing shown on a runtime without the ids
	using CreateFn = void (*)(void*, const char*, const char*, void*, std::uint32_t, const char*, const char*, const char*,
		const char*, bool);

	bool ShowBoxGuarded(CreateFn create, void* manager, const char* header, const char* body)
	{
		__try {
			create(manager, header, body, nullptr, 0, "OK", nullptr, nullptr, nullptr, false);
			return true;
		} __except (1) {
			return false;
		}
	}

	bool ShowBox(const std::string& body)
	{
		const auto singleton = Hook::Resolve(Hook::IdPair{ 959572, 4796373 });
		const auto create = Hook::Resolve(Hook::IdPair{ 89563, 2249456 });
		if (!singleton || !create)
			return false;
		void* manager = *reinterpret_cast<void**>(*singleton);
		if (!manager)
			return false;
		return ShowBoxGuarded(reinterpret_cast<CreateFn>(*create), manager, "Anatomy", body.c_str());
	}

	void Run()
	{
		Report r;
		const std::time_t t = std::time(nullptr);
		std::tm tm{};
		localtime_s(&tm, &t);
		char when[32];
		std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);
		r.text << "Anatomy health check, " << when << " (a save loaded " << kWait.count() << " s ago)\n\n";
		const auto bones = PresetBones();
		std::string kind;
		Engine(r);
		Preset(r, bones);
		Body(r, bones, kind);
		Builder(r, bones, kind);
		Actors(r);
		r.text << "\n" << (r.problems.empty() ? "No problems found.\n" : std::to_string(r.problems.size()) + " problem(s) found.\n");

		std::string where = "Anatomy_Health.txt";
		if (auto dir = rdlog::log_directory()) {
			*dir /= "Anatomy_Health.txt"sv;
			std::ofstream out(*dir, std::ios::binary | std::ios::trunc);
			out << r.text.str();
			where = dir->string();
		}
		rdlog::info("health: {} problem(s); report {}", r.problems.size(), where);
		if (r.problems.empty() || shown)
			return;
		std::string box = "Anatomy found " + std::to_string(r.problems.size()) + " problem(s):\n";
		for (auto& p : r.problems)
			box += "\n- " + p;
		box += "\n\nDetails and fixes: Documents\\My Games\\Fallout4\\F4SE\\Anatomy_Health.txt";
		shown = ShowBox(box);
		if (!shown)
			rdlog::warn("health: the message box could not be shown on this runtime");
	}
}

namespace Health
{
	void OnGameLoaded()
	{
		armed = true;
		frames = 0;
		loadedAt = Clock::now();
	}

	void Tick()
	{
		if (!armed || ++frames < kMinFrames || Clock::now() - loadedAt < kWait)
			return;
		armed = false;
		try {
			Run();
		} catch (const std::exception& e) {
			rdlog::warn("health: the check stopped: {}", e.what());
		}
	}
}
