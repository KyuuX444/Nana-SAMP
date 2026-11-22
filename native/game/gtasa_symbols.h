#pragma once

#include <cstdint>
#include <cstddef>

namespace nana::game {

// RenderWare / GTA SA CVector and CMatrix (verified against libGTASA.so 2.00 armeabi-v7a)
struct GtaVector {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct GtaQuaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct GtaMatrix {
    GtaVector right;
    uint32_t flags;
    GtaVector up;
    uint32_t pad1;
    GtaVector at;
    uint32_t pad2;
    GtaVector pos;
    uint32_t pad3;
};

// Verified ARM32 (armeabi-v7a) field offsets inside libGTASA.so (GTA SA Android 2.00 AML 1.4.0)
namespace GtaOffsets {
    constexpr size_t PLACEABLE_MATRIX          = 0x14; // GtaMatrix* (verified at CPhysical::ApplyGravity 0x3fe77a)
    constexpr size_t ENTITY_MODEL_INDEX        = 0x26; // int16_t
    constexpr size_t PHYSICAL_FLAGS            = 0x44; // uint32_t (verified at 0x3fe740)
    constexpr size_t PHYSICAL_MOVE_SPEED       = 0x48; // GtaVector (x=0x48, y=0x4C, z=0x50 verified at 0x3fe766)

    constexpr size_t PED_FLAGS                 = 0x484;
    constexpr size_t PED_IN_VEHICLE_FLAG_BYTE  = 0x485; // bit 0 = bInVehicle
    constexpr size_t PED_HEALTH                = 0x544; // float
    constexpr size_t PED_MAX_HEALTH            = 0x548; // float
    constexpr size_t PED_ARMOUR                = 0x54C; // float
    constexpr size_t PED_CURRENT_ROTATION      = 0x55C; // float (radians)
    constexpr size_t PED_AIMING_ROTATION       = 0x560; // float (radians)
    constexpr size_t PED_HEADING_CHANGE_RATE   = 0x564; // float
    constexpr size_t PED_VEHICLE_PTR           = 0x590; // void* CVehicle*
    constexpr size_t PED_WEAPONS_ARRAY         = 0x5A4; // 13 slots, stride 0x1C (28 bytes), type at +0x00
    constexpr size_t PED_WEAPON_SLOT_STRIDE    = 0x1C;
    constexpr size_t PED_ACTIVE_WEAPON_SLOT    = 0x71C; // uint8_t

    constexpr size_t VEHICLE_HEALTH            = 0x4CC; // float
    constexpr size_t VEHICLE_DRIVER_PTR        = 0x460; // void* CPed*

    // CCamera (TheCamera @ 0x00951fa8) active CCam matrix / front / pos
    constexpr size_t CAMERA_ACTIVE_CAM_IDX     = 0x0059; // uint8_t m_nActiveCam
    constexpr size_t CAMERA_CAMS_ARRAY         = 0x0174; // CCam m_aCams[3], stride 0x238
    constexpr size_t CAM_STRUCT_STRIDE         = 0x0238;
    constexpr size_t CAM_MODE                  = 0x000C; // uint16_t
    constexpr size_t CAM_FRONT                 = 0x0110; // GtaVector
    constexpr size_t CAM_SOURCE                = 0x011C; // GtaVector
    constexpr size_t CAM_UP                    = 0x0128; // GtaVector
    constexpr size_t CAM_FOV                   = 0x00B0; // float
}

/**
 * Dynamic symbol table resolved from libGTASA.so (GTA SA 2.00 armeabi-v7a) via dlsym.
 * Every mangled name is verified directly against readelf -Ws of libGTASA.so from
 * APK_GTA_SA_2.00_AML_1.4.0.zip.
 */
struct GtaSymbols {
    void* handle = nullptr;
    bool resolved = false;

    // Global state pointers
    int* gGameState = nullptr;
    int* gCurrArea = nullptr;
    void* gTheCamera = nullptr;
    void* gMobileMenu = nullptr;
    float* gLoadingPercent = nullptr;
    bool* gLoadingActive = nullptr;
    char* gStorageRootBuffer = nullptr;
    char* gStorageBaseRootBuffer = nullptr;
    void* gRqCaps = nullptr;
    void* gWorldPlayers = nullptr;

    // Lifecycle & hook targets
    void (*DoGameState)() = nullptr;
    void (*CGame_Process)() = nullptr;
    void (*MobileMenu_InitForTitle)(void* menu) = nullptr;
    void (*MobileMenu_Unload)(void* menu) = nullptr;
    void (*Menu_NewGame)() = nullptr;
    void (*Menu_SwitchOffToGame)() = nullptr;
    uint32_t (*MainMenuScreen_Update)(void* screen, float dt) = nullptr;
    void (*StartGameScreen_OnNewGameCheck)() = nullptr;
    void (*CLoadingScreen_DisplayPCScreen)() = nullptr;
    void (*CAudioEngine_StartLoadingTune)(void* engine) = nullptr;
    void (*CCutsceneMgr_Initialise)() = nullptr;
    void (*CPopulation_Initialise)() = nullptr;
    void (*CGangs_Initialise)() = nullptr;
    void (*CAEGlobalWeaponAudioEntity_ServiceAmbientGunFire)(void* ent) = nullptr;
    void (*CAEPedSpeechAudioEntity_AddSayEvent)(void* ent, int a, int16_t b, uint32_t c, float d, uint8_t e, uint8_t f, uint8_t g) = nullptr;
    void (*CPopulation_ManagePopulation)() = nullptr;
    void (*CCarCtrl_GenerateRandomCars)() = nullptr;
    void (*CTheScripts_Process)() = nullptr;
    void* (*TextureDatabaseRuntime_Load)(const char* name, bool fullyLoad, int format) = nullptr;
    void (*TextureDatabaseRuntime_Register)(void* db) = nullptr;
    void* (*NvFOpen)(const char* dir, const char* filename, bool isUserFile, bool checkApk) = nullptr;

    // Streaming & IMG archives
    void (*CStreaming_InitImageList)() = nullptr;
    int (*CStreaming_AddImageToList)(const char* filename, bool bNotPlayerImg) = nullptr;
    void (*CStreaming_RequestModel)(int modelId, int flags) = nullptr;
    void (*CStreaming_LoadAllRequestedModels)(bool priorityOnly) = nullptr;

    // Model classification
    bool (*CModelInfo_IsCarModel)(int modelId) = nullptr;
    bool (*CModelInfo_IsBikeModel)(int modelId) = nullptr;
    bool (*CModelInfo_IsBoatModel)(int modelId) = nullptr;
    bool (*CModelInfo_IsHeliModel)(int modelId) = nullptr;
    bool (*CModelInfo_IsPlaneModel)(int modelId) = nullptr;
    bool (*CModelInfo_IsBmxModel)(int modelId) = nullptr;
    bool (*CModelInfo_IsQuadBikeModel)(int modelId) = nullptr;
    bool (*CModelInfo_IsTrailerModel)(int modelId) = nullptr;

    // Player & Entity access
    void* (*FindPlayerPed)(int playerId) = nullptr;
    void (*CWorld_Add)(void* entity) = nullptr;
    void (*CWorld_Remove)(void* entity) = nullptr;
    float (*CWorld_FindGroundZForCoord)(float x, float y) = nullptr;
    void (*CEntity_Teleport)(void* entity, GtaVector pos, uint8_t resetRotation) = nullptr;

    // Ped methods & pool allocators
    void* (*CPed_OperatorNew)(uint32_t size) = nullptr;
    void (*CPed_OperatorDelete)(void* ptr) = nullptr;
    void (*CCivilianPed_Ctor)(void* ped, int pedType, uint32_t modelIndex) = nullptr;
    void (*CPed_Teleport)(void* ped, GtaVector pos, uint8_t resetRotation) = nullptr;
    void (*CPed_SetModelIndex)(void* ped, uint32_t modelIndex) = nullptr;
    void (*CPed_GiveWeapon)(void* ped, int weaponType, uint32_t ammo, bool likeUnused) = nullptr;
    void (*CPed_ClearWeapons)(void* ped) = nullptr;
    void (*CPed_SetCurrentWeapon)(void* ped, int slot) = nullptr;

    // Vehicle methods & pool allocators (verified 2-arg vs 3-arg ARM32 ctors!)
    void* (*CVehicle_OperatorNew)(uint32_t size) = nullptr;
    void (*CVehicle_OperatorDelete)(void* ptr) = nullptr;
    void (*CAutomobile_Ctor)(void* veh, int modelIndex, uint8_t createdBy, uint8_t setupSuspension) = nullptr;
    void (*CBike_Ctor)(void* veh, int modelIndex, uint8_t createdBy) = nullptr;
    void (*CBmx_Ctor)(void* veh, int modelIndex, uint8_t createdBy) = nullptr;
    void (*CQuadBike_Ctor)(void* veh, int modelIndex, uint8_t createdBy) = nullptr;
    void (*CBoat_Ctor)(void* veh, int modelIndex, uint8_t createdBy) = nullptr;
    void (*CHeli_Ctor)(void* veh, int modelIndex, uint8_t createdBy) = nullptr;
    void (*CPlane_Ctor)(void* veh, int modelIndex, uint8_t createdBy) = nullptr;
    void (*CTrailer_Ctor)(void* veh, int modelIndex, uint8_t createdBy) = nullptr;

    // World Objects
    void* (*CObject_Create)(int modelIndex, bool bUnused) = nullptr;

    // Camera, World Time, Weather, Radar
    void (*CCamera_SetCamPositionForFixedMode)(void* cam, const GtaVector& pos, const GtaVector& offset) = nullptr;
    void (*CCamera_TakeControlNoEntity)(void* cam, const GtaVector& lookAt, int16_t switchStyle, int whoIsInControl) = nullptr;
    void (*CCamera_RestoreWithJumpCut)(void* cam) = nullptr;
    void (*CClock_SetGameClock)(uint8_t hour, uint8_t minute, uint8_t day) = nullptr;
    void (*CWeather_ForceWeatherNow)(int16_t weatherId) = nullptr;
    int (*CRadar_SetShortRangeCoordBlip)(int blipType, GtaVector pos, uint32_t color, int display, char* scriptName) = nullptr;
    void (*CRadar_ClearBlip)(int blipIndex) = nullptr;

    // Pad input reading
    void* (*CPad_GetPad)(int padNum) = nullptr;
    int16_t (*CPad_GetPedWalkLeftRight)(void* pad) = nullptr;
    int16_t (*CPad_GetPedWalkUpDown)(void* pad) = nullptr;
    int16_t (*CPad_GetSteeringLeftRight)(void* pad) = nullptr;
    uint8_t (*CPad_GetHandBrake)(void* pad) = nullptr;
    int16_t (*CPad_GetAccelerate)(void* pad) = nullptr;
    bool (*CPad_GetExitVehicle)(void* pad) = nullptr;

    bool Resolve();
};

} // namespace nana::game
