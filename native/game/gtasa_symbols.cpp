#include "gtasa_symbols.h"

#include <dlfcn.h>

#ifdef __ANDROID__
#include <android/log.h>
#define NANA_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NanaSAMP-Syms", __VA_ARGS__)
#define NANA_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "NanaSAMP-Syms", __VA_ARGS__)
#else
#include <cstdio>
#define NANA_LOGI(...) do { std::printf("[NanaSAMP-Syms] "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define NANA_LOGE(...) do { std::fprintf(stderr, "[NanaSAMP-Syms ERROR] "); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#endif

namespace nana::game {

namespace {

template <typename T>
bool ResolveSym(void* lib, T& out, const char* name) {
    void* p = dlsym(lib, name);
    if (!p) {
        p = dlsym(RTLD_DEFAULT, name);
    }
    out = reinterpret_cast<T>(p);
    return p != nullptr;
}

} // namespace

bool GtaSymbols::Resolve() {
    handle = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_LAZY);
    if (!handle) {
        handle = dlopen("libGTASA.so", RTLD_LAZY);
    }
    if (!handle) {
        NANA_LOGE("Failed to open libGTASA.so: %s", dlerror());
        return false;
    }

    int okCount = 0;
    auto bind = [&](auto& field, const char* sym) {
        if (ResolveSym(handle, field, sym)) okCount++;
    };

    // Globals
    bind(gGameState, "gGameState");
    bind(gCurrArea, "_ZN5CGame8currAreaE");
    bind(gTheCamera, "TheCamera");
    bind(gMobileMenu, "gMobileMenu");
    bind(gLoadingPercent, "_ZN14CLoadingScreen15m_PercentLoadedE");
    bind(gLoadingActive, "_ZN14CLoadingScreen9m_bActiveE");
    bind(gStorageRootBuffer, "StorageRootBuffer");
    bind(gStorageBaseRootBuffer, "StorageBaseRootBuffer");
    bind(gRqCaps, "RQCaps");
    bind(gWorldPlayers, "_ZN6CWorld7PlayersE");

    // Lifecycle & hooks
    bind(DoGameState, "_Z11DoGameStatev");
    bind(CGame_Process, "_ZN5CGame7ProcessEv");
    bind(MobileMenu_InitForTitle, "_ZN10MobileMenu12InitForTitleEv");
    bind(MobileMenu_Unload, "_ZN10MobileMenu6UnloadEv");
    bind(Menu_NewGame, "_Z12Menu_NewGamev");
    bind(Menu_SwitchOffToGame, "_Z20Menu_SwitchOffToGamev");
    bind(MainMenuScreen_Update, "_ZN14MainMenuScreen6UpdateEf");
    bind(StartGameScreen_OnNewGameCheck, "_ZN15StartGameScreen14OnNewGameCheckEv");
    bind(CLoadingScreen_DisplayPCScreen, "_ZN14CLoadingScreen15DisplayPCScreenEv");
    bind(CAudioEngine_StartLoadingTune, "_ZN12CAudioEngine16StartLoadingTuneEv");
    bind(CCutsceneMgr_Initialise, "_ZN12CCutsceneMgr10InitialiseEv");
    bind(CPopulation_Initialise, "_ZN11CPopulation10InitialiseEv");
    bind(CGangs_Initialise, "_ZN6CGangs10InitialiseEv");
    bind(CAEGlobalWeaponAudioEntity_ServiceAmbientGunFire,
         "_ZN26CAEGlobalWeaponAudioEntity21ServiceAmbientGunFireEv");
    bind(CAEPedSpeechAudioEntity_AddSayEvent,
         "_ZN23CAEPedSpeechAudioEntity11AddSayEventEisjfhhh");
    bind(CPopulation_ManagePopulation, "_ZN11CPopulation16ManagePopulationEv");
    bind(CCarCtrl_GenerateRandomCars, "_ZN8CCarCtrl18GenerateRandomCarsEv");
    bind(CTheScripts_Process, "_ZN11CTheScripts7ProcessEv");
    bind(TextureDatabaseRuntime_Load,
         "_ZN22TextureDatabaseRuntime4LoadEPKcb21TextureDatabaseFormat");
    bind(TextureDatabaseRuntime_Register,
         "_ZN22TextureDatabaseRuntime8RegisterEPS_");
    bind(NvFOpen, "_Z7NvFOpenPKcS0_bb");

    // Streaming & IMG
    bind(CStreaming_InitImageList, "_ZN10CStreaming13InitImageListEv");
    bind(CStreaming_AddImageToList, "_ZN10CStreaming14AddImageToListEPKcb");
    bind(CStreaming_RequestModel, "_ZN10CStreaming12RequestModelEii");
    bind(CStreaming_LoadAllRequestedModels, "_ZN10CStreaming22LoadAllRequestedModelsEb");

    // ModelInfo classification
    bind(CModelInfo_IsCarModel, "_ZN10CModelInfo10IsCarModelEi");
    bind(CModelInfo_IsBikeModel, "_ZN10CModelInfo11IsBikeModelEi");
    bind(CModelInfo_IsBoatModel, "_ZN10CModelInfo11IsBoatModelEi");
    bind(CModelInfo_IsHeliModel, "_ZN10CModelInfo11IsHeliModelEi");
    bind(CModelInfo_IsPlaneModel, "_ZN10CModelInfo12IsPlaneModelEi");
    bind(CModelInfo_IsBmxModel, "_ZN10CModelInfo10IsBmxModelEi");
    bind(CModelInfo_IsQuadBikeModel, "_ZN10CModelInfo15IsQuadBikeModelEi");
    bind(CModelInfo_IsTrailerModel, "_ZN10CModelInfo14IsTrailerModelEi");

    // Player & Entities
    bind(FindPlayerPed, "_Z13FindPlayerPedi");
    bind(CWorld_Add, "_ZN6CWorld3AddEP7CEntity");
    bind(CWorld_Remove, "_ZN6CWorld6RemoveEP7CEntity");
    bind(CWorld_FindGroundZForCoord, "_ZN6CWorld19FindGroundZForCoordEff");
    bind(CEntity_Teleport, "_ZN7CEntity8TeleportE7CVectorh");

    // Peds
    bind(CPed_OperatorNew, "_ZN4CPednwEj");
    bind(CPed_OperatorDelete, "_ZN4CPeddlEPv");
    bind(CCivilianPed_Ctor, "_ZN12CCivilianPedC1E8ePedTypej");
    bind(CPed_Teleport, "_ZN4CPed8TeleportE7CVectorh");
    bind(CPed_SetModelIndex, "_ZN4CPed13SetModelIndexEj");
    bind(CPed_GiveWeapon, "_ZN4CPed10GiveWeaponE11eWeaponTypejb");
    bind(CPed_ClearWeapons, "_ZN4CPed12ClearWeaponsEv");
    bind(CPed_SetCurrentWeapon, "_ZN4CPed16SetCurrentWeaponEi");

    // Vehicles (verified exact ARM32 mangled names)
    bind(CVehicle_OperatorNew, "_ZN8CVehiclenwEj");
    bind(CVehicle_OperatorDelete, "_ZN8CVehicledlEPv");
    bind(CAutomobile_Ctor, "_ZN11CAutomobileC1Eihh");
    bind(CBike_Ctor, "_ZN5CBikeC1Eih");
    bind(CBmx_Ctor, "_ZN4CBmxC1Eih");
    bind(CQuadBike_Ctor, "_ZN9CQuadBikeC1Eih");
    bind(CBoat_Ctor, "_ZN5CBoatC1Eih");
    bind(CHeli_Ctor, "_ZN5CHeliC1Eih");
    bind(CPlane_Ctor, "_ZN6CPlaneC1Eih");
    bind(CTrailer_Ctor, "_ZN8CTrailerC1Eih");

    // Objects
    bind(CObject_Create, "_ZN7CObject6CreateEib");

    // Camera, Clock, Weather, Radar (verified exact ARM32 mangled names)
    bind(CCamera_SetCamPositionForFixedMode, "_ZN7CCamera26SetCamPositionForFixedModeERK7CVectorS2_");
    bind(CCamera_TakeControlNoEntity, "_ZN7CCamera19TakeControlNoEntityERK7CVectorsi");
    bind(CCamera_RestoreWithJumpCut, "_ZN7CCamera18RestoreWithJumpCutEv");
    bind(CClock_SetGameClock, "_ZN6CClock12SetGameClockEhhh");
    bind(CWeather_ForceWeatherNow, "_ZN8CWeather15ForceWeatherNowEs");
    bind(CRadar_SetShortRangeCoordBlip,
         "_ZN6CRadar22SetShortRangeCoordBlipE9eBlipType7CVectorj12eBlipDisplayPc");
    bind(CRadar_ClearBlip, "_ZN6CRadar9ClearBlipEi");

    // Pad input (verified exact ARM32 mangled names)
    bind(CPad_GetPad, "_ZN4CPad6GetPadEi");
    bind(CPad_GetPedWalkLeftRight, "_ZN4CPad19GetPedWalkLeftRightEv");
    bind(CPad_GetPedWalkUpDown, "_ZN4CPad16GetPedWalkUpDownEv");
    bind(CPad_GetSteeringLeftRight, "_ZN4CPad20GetSteeringLeftRightEv");
    bind(CPad_GetHandBrake, "_ZN4CPad12GetHandBrakeEv");
    bind(CPad_GetAccelerate, "_ZN4CPad13GetAccelerateEv");
    bind(CPad_GetExitVehicle, "_ZN4CPad14GetExitVehicleEv");

    resolved = (CGame_Process != nullptr && FindPlayerPed != nullptr && DoGameState != nullptr);
    NANA_LOGI("Resolved %d GTA SA 2.00 symbols (core=%d, NvFOpen=%p, TexDB=%p, InitImg=%p)",
              okCount, resolved ? 1 : 0,
              reinterpret_cast<void*>(NvFOpen),
              reinterpret_cast<void*>(TextureDatabaseRuntime_Load),
              reinterpret_cast<void*>(CStreaming_InitImageList));
    return resolved;
}

} // namespace nana::game
