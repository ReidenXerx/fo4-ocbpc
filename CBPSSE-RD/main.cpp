// fo4-anatomy: the engine's entry points on Runtime Database (CommonLibF4RD): one DLL for OG 1.10.163, NG 1.10.984 and
// AE 1.11.x. The plugin keeps the classic build's name, "OCBPC plugin": Rapport's face authority is sent to it by name
// (F4SE messaging), on every runtime. Load does what the classic build's did, in the same order.
#include "PCH.h"
#include "Aim.h"
#include "Bones.h"
#include "Eyes.h"
#include "Hook.h"
#include "Mouth.h"
#include "PapyrusOCBP.h"
#include "config.h"

namespace
{
	constexpr auto kName = "OCBPC plugin"sv;
	constexpr std::uint32_t kClassicVersion = 24;   // the classic build's PluginInfo version

	[[nodiscard]] constexpr std::uint32_t PackVersion(std::uint32_t a_major, std::uint32_t a_minor, std::uint32_t a_patch) noexcept
	{
		return ((a_major & 0xFF) << 24) | ((a_minor & 0xFF) << 16) | ((a_patch & 0xFFF) << 4);
	}

	[[nodiscard]] constexpr F4SE::PluginVersionData MakeVersionData() noexcept
	{
		F4SE::PluginVersionData data{};
		data.pluginVersion = PackVersion(OCBPC_VERSION_MAJOR, OCBPC_VERSION_MINOR, OCBPC_VERSION_PATCH);
		for (std::size_t i = 0; i < kName.size() && i < std::size(data.name) - 1; ++i) {
			data.name[i] = kName[i];
		}
		data.addressIndependence = F4SE::PluginVersionData::kAddressIndependence_Signatures;
		// the layouts this plugin reads are checked at run time (Layout.cpp, fail closed), not assumed
		data.structureIndependence = F4SE::PluginVersionData::kStructureIndependence_1_10_980Layout |
		                             F4SE::PluginVersionData::kStructureIndependence_1_11_137Layout;
		return data;
	}

	bool InitializeLogger()
	{
		auto path = rdlog::log_directory();
		if (!path) {
			return false;
		}
		*path /= "cbp.log"sv;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v"s);
		return true;
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm)
	{
		papyrusOCBP::RegisterFuncs(a_vm);
		RegisterAimFuncs(a_vm);   // fo4-anatomy (A-28): AnatomyAim.SetBusy, from Anatomy:Arousal
		return true;
	}

	void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case F4SE::MessagingInterface::kGameDataReady:
			WatchLoadedActors();   // fo4-anatomy (A-44): Bones.h
			break;
		case F4SE::MessagingInterface::kNewGame:
			ReleaseAllFaces("a new game");   // nothing Rapport held survives into another game
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			ReleaseAllFaces("a save is loading");
			ResetAims();   // fo4-anatomy (A-28): the new skeletons carry nothing we wrote
			break;
		case F4SE::MessagingInterface::kPostLoad:
			ListenForFaces(F4SE::GetMessagingInterface());   // every plugin is loaded, Rapport too
			break;
		case F4SE::MessagingInterface::kPostPostLoad:
			SayFaceHello();
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
			// again: a face Rapport sent while the load ran (after PreLoadGame's release) would outlive the save
			ReleaseAllFaces("a save finished loading");
			StartFaceAuthorityTest();
			break;
		default:
			break;
		}
	}
}

extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = MakeVersionData();

// OG's F4SE (0.6.23) asks this; NG's and AE's read F4SEPlugin_Version. It must not refuse another runtime: the
// runtime is decided by Runtime Database, and a refusal here would be the classic build's behaviour.
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = kName.data();
	a_info->version = kClassicVersion;
	return !a_f4se->IsEditor();
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	if (!InitializeLogger()) {
		return false;
	}
	if (a_f4se->IsEditor()) {
		return false;
	}
	F4SE::Init(a_f4se);
	rdlog::info("{} {} (Runtime Database) on Fallout 4 {}", kName, OCBPC_VERSION_STRING,
		REL::Module::get().version().string());   // "1-10-163-0": RD prints dashes

	if (auto* papyrus = F4SE::GetPapyrusInterface(); !papyrus || !papyrus->Register(RegisterFuncs)) {
		rdlog::warn("Papyrus: natives not registered (OCBP_API, AnatomyAim)");
	}

	// the config before the hook, as the classic build
	LoadConfig();
	LoadCollisionConfig();
	Hook::InstallFrame();
	// glances first, then the mouth (phase 2 of the port: not yet on this build)
	InstallEyeHook();
	InstallMouthHook();
	// last, after the hooks (the classic build's reason: a listener registered before a fault in Load would be
	// called in a DLL that is gone)
	if (auto* messaging = F4SE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(MessageHandler)) {
		rdlog::warn("F4SE messages: not listening (no face authority, no load watch)");
	}
	return true;
}
