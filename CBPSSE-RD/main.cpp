// fo4-anatomy: the engine's entry points on Runtime Database (CommonLibF4RD): one DLL for OG 1.10.163, NG 1.10.984 and
// AE 1.11.x. The plugin keeps the classic build's name, "OCBPC plugin": Rapport's face authority is sent to it by name
// (F4SE messaging), on every runtime.
#include "PCH.h"

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
		// the layouts this plugin reads are checked at run time (fail closed), not assumed
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
	return true;
}
