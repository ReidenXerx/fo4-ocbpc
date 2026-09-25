// fo4-ocbpc: modified by fo4-anatomy (ReidenXerx), 2026-09-23: installs the mouth hook; 2026-09-24: F4SE's
// messages carry Rapport's face authority (Mouth.h).
// The original OpenCBP_FO4 / OCBPC code is under the MIT licence (LICENSE); these changes
// are under the GNU General Public License, version 3 (COPYING), with the additional
// permission for F4SE stated in README.md.
#include "common/ITypes.h"
#include <string>
#include "f4se/PluginAPI.h"
#include "f4se_common/f4se_version.h"
#include "f4se_common/SafeWrite.h"
#include "f4se/GameAPI.h"
#include "f4se/GameEvents.h"
#include "log.h"
#include "config.h"
#include "PapyrusOCBP.h"
#include "Mouth.h"
#include "Aim.h"
#include "Eyes.h"


bool RegisterFuncs(VirtualMachine* vm);

PluginHandle	g_pluginHandle = kPluginHandle_Invalid;
F4SEMessagingInterface	* g_messaging = nullptr;

//F4SEScaleformInterface		* g_scaleform = NULL;
//F4SESerializationInterface	* g_serialization = NULL;
F4SETaskInterface				* g_task = nullptr;
F4SEPapyrusInterface            * g_papyrus = nullptr;
//IDebugLog	gLog("Data\\F4SE\\Plugins\\hook.log");


void DoHook();



void MessageHandler(F4SEMessagingInterface::Message * msg)
{
    switch (msg->type)
    {
        case F4SEMessagingInterface::kMessage_GameDataReady:
        {
            logger.Info("kMessage_GameDataReady\n");
        }
        break;
        case F4SEMessagingInterface::kMessage_GameLoaded:
        {
            logger.Info("kMessage_GameLoaded\n");
        }
        break;
        case F4SEMessagingInterface::kMessage_NewGame:
        {
            logger.Info("kMessage_NewGame\n");
            ReleaseAllFaces("a new game");   // fo4-anatomy: nothing Rapport held survives into another game
        }
        break;
        case F4SEMessagingInterface::kMessage_PreLoadGame:
        {
            logger.Info("kMessage_PreLoadGame\n");
            ReleaseAllFaces("a save is loading");
            ResetAims();   // fo4-anatomy (A-28): the new skeletons carry nothing we wrote
        }
        break;
        case F4SEMessagingInterface::kMessage_PostLoad:
        {
            logger.Info("kMessage_PostLoad\n");
            ListenForFaces(g_messaging, g_pluginHandle);   // fo4-anatomy: every plugin is loaded, Rapport too
        }
        break;
        case F4SEMessagingInterface::kMessage_PostPostLoad:
        {
            logger.Info("kMessage_PostPostLoad\n");
            SayFaceHello();
        }
        break;
        case F4SEMessagingInterface::kMessage_PostLoadGame:
        {
            logger.Info("kMessage_PostLoadGame\n");
            // again: a face Rapport sent while the load ran (after PreLoadGame's release) would outlive
            // the save it belonged to, and Rapport forgets its own on a load
            ReleaseAllFaces("a save finished loading");
            StartFaceAuthorityTest();
        }
        break;
        case F4SEMessagingInterface::kMessage_PreSaveGame:
        {
            logger.Info("kMessage_PreSaveGame\n");
        }
        break;
        case F4SEMessagingInterface::kMessage_PostSaveGame:
        {
            logger.Info("kMessage_PostSaveGame\n");
        }
        break;
        case F4SEMessagingInterface::kMessage_DeleteGame:
        {
            logger.Info("kMessage_DeleteGame\n");
        }
        break;
        case F4SEMessagingInterface::kMessage_InputLoaded:
        {
            logger.Info("kMessage_InputLoaded\n");
        }
        break;

    }
}


extern "C"
{

    bool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info)
    {
        logger.Info("OCBPC Physics F4SE Plugin\n");
        logger.Error("Query called\n");


        // populate info structure
        info->infoVersion = PluginInfo::kInfoVersion;
        info->name = "OCBPC plugin";
        info->version = 24;

        // store plugin handle so we can identify ourselves later
        g_pluginHandle = f4se->GetPluginHandle();

        if (f4se->isEditor)
        {
            logger.Error("loaded in editor, marking as incompatible\n");
            return false;
        }
        else if (f4se->runtimeVersion != RUNTIME_VERSION)
        {
            logger.Error("unsupported runtime version %08X", f4se->runtimeVersion);
            return false;
        }
        // supported runtime version

        g_papyrus = (F4SEPapyrusInterface*)f4se->QueryInterface(kInterface_Papyrus);
        if (!g_papyrus)
        {
            _WARNING("couldn't get papyrus interface");
        }

        logger.Error("Query complete\n");
        return true;
    }

    bool F4SEPlugin_Load(const F4SEInterface * f4se)
    {
        logger.Error("CBPC Loading\n");

        g_task = (F4SETaskInterface *)f4se->QueryInterface(kInterface_Task);
        if (!g_task)
        {
            logger.Error("Couldn't get Task interface\n");
            return false;
        }

        if (g_papyrus)
            g_papyrus->Register(RegisterFuncs);

        // Load initial config before the hook.
        logger.Error("Loading Config\n");
        LoadConfig();
        LoadCollisionConfig();
        logger.Error("Hooking Game\n");
        DoHook();
        // fo4-anatomy: glances first (ocbp.ini [Eyes]), under a guard: were anything to fault after a call is
        // patched, F4SE would unload this plugin and leave the engine calling into a DLL that is gone
        InstallEyeHook();
        InstallMouthHook();   // fo4-anatomy: the mouth (ocbp.ini [Mouth]); checks the build first
        // fo4-anatomy: F4SE's own messages (PostLoad, loads) drive Rapport's face authority. Last, after
        // the hooks: F4SE unloads a plugin whose Load faults, and a listener registered before the fault
        // would be called in a DLL that is gone.
        g_messaging = (F4SEMessagingInterface *)f4se->QueryInterface(kInterface_Messaging);
        if (!g_messaging || !g_messaging->RegisterListener(g_pluginHandle, "F4SE", MessageHandler))
            logger.Error("Couldn't listen to F4SE's messages: no face authority\n");
        logger.Error("CBP Load Complete\n");
        return true;
    }
};

bool RegisterFuncs(VirtualMachine* vm)
{
    papyrusOCBP::RegisterFuncs(vm);
    RegisterAimFuncs(vm);   // fo4-anatomy (A-28): AnatomyAim.SetBusy, from Anatomy:Arousal
    return true;
}

BOOL WINAPI DllMain(
    _In_ HINSTANCE hinstDLL,
    _In_ DWORD     fdwReason,
    _In_ LPVOID    lpvReserved
) {
    return true;
}