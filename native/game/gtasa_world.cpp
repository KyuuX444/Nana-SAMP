#include "gtasa_world.h"
#include "aml_compat.h"
#include "arm_hook.h"
#include "file_compat.h"
#include "gpu_compat.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/log.h>
#define WORLD_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "NanaSAMP-World", __VA_ARGS__)
#define WORLD_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "NanaSAMP-World", __VA_ARGS__)
#else
#include <cstdio>
#define WORLD_LOGI(...) do { std::printf("[NanaSAMP-World] "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define WORLD_LOGW(...) do { std::fprintf(stderr, "[NanaSAMP-World WARN] "); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#endif

namespace nana::game {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

// Safe memory field accessors using verified libGTASA.so 2.00 ARM32 offsets
GtaMatrix* EntityGetMatrix(void* entity) {
    if (!entity) return nullptr;
    return *reinterpret_cast<GtaMatrix**>(static_cast<uint8_t*>(entity) + GtaOffsets::PLACEABLE_MATRIX);
}

GtaVector& PhysicalMoveSpeed(void* entity) {
    return *reinterpret_cast<GtaVector*>(static_cast<uint8_t*>(entity) + GtaOffsets::PHYSICAL_MOVE_SPEED);
}

bool PedIsInVehicle(void* ped) {
    if (!ped) return false;
    uint8_t flag = *(static_cast<uint8_t*>(ped) + GtaOffsets::PED_IN_VEHICLE_FLAG_BYTE);
    void* veh = *reinterpret_cast<void**>(static_cast<uint8_t*>(ped) + GtaOffsets::PED_VEHICLE_PTR);
    return ((flag & 0x01u) != 0) && (veh != nullptr);
}

void* PedGetVehicle(void* ped) {
    if (!ped) return nullptr;
    return *reinterpret_cast<void**>(static_cast<uint8_t*>(ped) + GtaOffsets::PED_VEHICLE_PTR);
}

void* VehicleGetDriver(void* veh) {
    if (!veh) return nullptr;
    return *reinterpret_cast<void**>(static_cast<uint8_t*>(veh) + GtaOffsets::VEHICLE_DRIVER_PTR);
}

float& PedHealth(void* ped) {
    return *reinterpret_cast<float*>(static_cast<uint8_t*>(ped) + GtaOffsets::PED_HEALTH);
}

float& PedArmour(void* ped) {
    return *reinterpret_cast<float*>(static_cast<uint8_t*>(ped) + GtaOffsets::PED_ARMOUR);
}

float& PedCurrentRotation(void* ped) {
    return *reinterpret_cast<float*>(static_cast<uint8_t*>(ped) + GtaOffsets::PED_CURRENT_ROTATION);
}

float& PedAimingRotation(void* ped) {
    return *reinterpret_cast<float*>(static_cast<uint8_t*>(ped) + GtaOffsets::PED_AIMING_ROTATION);
}

uint8_t PedGetCurrentWeaponId(void* ped) {
    if (!ped) return 0;
    uint8_t slot = *(static_cast<uint8_t*>(ped) + GtaOffsets::PED_ACTIVE_WEAPON_SLOT);
    if (slot >= 13) return 0;
    uint32_t weapType = *reinterpret_cast<uint32_t*>(
        static_cast<uint8_t*>(ped) + GtaOffsets::PED_WEAPONS_ARRAY + slot * GtaOffsets::PED_WEAPON_SLOT_STRIDE);
    return static_cast<uint8_t>(weapType & 0xFFu);
}

float& VehicleHealth(void* veh) {
    return *reinterpret_cast<float*>(static_cast<uint8_t*>(veh) + GtaOffsets::VEHICLE_HEALTH);
}

void SetMatrixZHeading(GtaMatrix* mat, float angleDeg) {
    if (!mat) return;
    float rad = angleDeg * kDegToRad;
    float c = std::cos(rad);
    float s = std::sin(rad);
    mat->right = GtaVector{c, s, 0.0f};
    mat->up = GtaVector{-s, c, 0.0f};
    mat->at = GtaVector{0.0f, 0.0f, 1.0f};
}

// Static hook trampolines
void Hook_DoGameState() { GtaSaWorld::Instance().OnDoGameStateHook(); }
void Hook_CGame_Process() { GtaSaWorld::Instance().OnGameProcessFrame(); }
void Hook_CStreaming_InitImageList() { GtaSaWorld::Instance().OnInitImageListHook(); }
void Hook_MobileMenu_InitForTitle(void* menu) { GtaSaWorld::Instance().OnMobileMenuInitTitle(menu); }
uint32_t Hook_MainMenuScreen_Update(void* screen, float dt) {
    return GtaSaWorld::Instance().OnMainMenuScreenUpdate(screen, dt);
}
void Hook_NoOpVoid() {}
void Hook_NoOpOnePtr(void*) {}
void Hook_NoOpSpeech(void*, int, int16_t, uint32_t, float, uint8_t, uint8_t, uint8_t) {}

uint16_t ReadLocalPadKeys(const GtaSymbols& syms, void* pad, bool inVehicle) {
    if (!pad) return 0;
    uint16_t keys = 0;
    if (inVehicle) {
        if (syms.CPad_GetAccelerate && syms.CPad_GetAccelerate(pad) > 0) {
            keys |= nana::net::SampKeys::SPRINT;
        }
        if (syms.CPad_GetHandBrake && syms.CPad_GetHandBrake(pad) != 0) {
            keys |= nana::net::SampKeys::HANDBRAKE;
        }
    } else {
        if (syms.CPad_GetHandBrake && syms.CPad_GetHandBrake(pad) != 0) {
            keys |= nana::net::SampKeys::HANDBRAKE;
        }
    }
    return keys;
}

bool FileExistsRegular(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

GtaSaWorld& GtaSaWorld::Instance() {
    static GtaSaWorld inst;
    return inst;
}

void GtaSaWorld::SetCallbacks(UiEventCallbacks cb) {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    callbacks_ = std::move(cb);
}

bool GtaSaWorld::InitializeHooks(const std::string& storageRoot,
                                 int preferredTexFormat,
                                 const std::string& gpuRenderer,
                                 const std::string& gpuExtensions) {
    storageRoot_ = storageRoot;
    if (hooksInstalled_) return true;

    // 1. Probe AML 1.4.0 state (non-intrusive, keeps AML fully active)
    AmlCompat::Instance().ProbeAndConfigure(storageRoot);

    if (!syms_.Resolve()) {
        WORLD_LOGW("Failed to resolve GTA SA 2.00 symbols; hooks not installed");
        return false;
    }

    // 2. Ensure GTA SA's StorageRootBuffer has a trailing slash
    if (!storageRoot.empty()) {
        std::string rootWithSlash = storageRoot;
        if (rootWithSlash.back() != '/') rootWithSlash.push_back('/');
        if (syms_.gStorageRootBuffer) {
            std::strncpy(syms_.gStorageRootBuffer, rootWithSlash.c_str(), 511);
            syms_.gStorageRootBuffer[511] = '\0';
        }
        if (syms_.gStorageBaseRootBuffer) {
            std::strncpy(syms_.gStorageBaseRootBuffer, rootWithSlash.c_str(), 511);
            syms_.gStorageBaseRootBuffer[511] = '\0';
        }
    }

    // 3. Install NvFOpen SA-MP override & case-insensitive file resolver
    FileCompat::Instance().Configure(storageRoot);
    if (syms_.NvFOpen) {
        FileCompat::Instance().InstallHooks(reinterpret_cast<void*>(syms_.NvFOpen));
    }

    // 4. Install GPU & TextureDatabaseRuntime compatibility layer
    GpuCompat::Instance().Configure(storageRoot, preferredTexFormat, gpuRenderer, gpuExtensions);
    if (syms_.TextureDatabaseRuntime_Load) {
        GpuCompat::Instance().InstallHooks(
            syms_.gRqCaps,
            reinterpret_cast<void*>(syms_.TextureDatabaseRuntime_Load));
    }

    // 5. Hook CStreaming::InitImageList to auto-mount Alyn SA-MP samp.img & SAMPCOL.img
    if (syms_.CStreaming_InitImageList) {
        ArmHook::Install(
            reinterpret_cast<void*>(syms_.CStreaming_InitImageList),
            reinterpret_cast<void*>(&Hook_CStreaming_InitImageList),
            reinterpret_cast<void**>(&origInitImageList_));
    }

    // 6. Core lifecycle hooks (DoGameState + CGame::Process)
    if (syms_.DoGameState) {
        ArmHook::Install(
            reinterpret_cast<void*>(syms_.DoGameState),
            reinterpret_cast<void*>(&Hook_DoGameState),
            reinterpret_cast<void**>(&origDoGameState_));
    }

    if (syms_.CGame_Process) {
        ArmHook::Install(
            reinterpret_cast<void*>(syms_.CGame_Process),
            reinterpret_cast<void*>(&Hook_CGame_Process),
            reinterpret_cast<void**>(&origCGameProcess_));
    }

    // 7. Suppress singleplayer title menu, NPC traffic, ambient speech, and story missions
    if (syms_.MobileMenu_InitForTitle) {
        ArmHook::Install(
            reinterpret_cast<void*>(syms_.MobileMenu_InitForTitle),
            reinterpret_cast<void*>(&Hook_MobileMenu_InitForTitle),
            reinterpret_cast<void**>(&origMobileMenuInitTitle_));
    }
    if (syms_.MainMenuScreen_Update) {
        ArmHook::Install(
            reinterpret_cast<void*>(syms_.MainMenuScreen_Update),
            reinterpret_cast<void*>(&Hook_MainMenuScreen_Update),
            reinterpret_cast<void**>(&origMainMenuScreenUpdate_));
    }

    auto suppressVoid = [](auto fn) {
        if (fn) ArmHook::Install(reinterpret_cast<void*>(fn), reinterpret_cast<void*>(&Hook_NoOpVoid), nullptr);
    };
    auto suppressOnePtr = [](auto fn) {
        if (fn) ArmHook::Install(reinterpret_cast<void*>(fn), reinterpret_cast<void*>(&Hook_NoOpOnePtr), nullptr);
    };

    suppressVoid(syms_.CPopulation_ManagePopulation);
    suppressVoid(syms_.CCarCtrl_GenerateRandomCars);
    suppressVoid(syms_.CTheScripts_Process);
    suppressVoid(syms_.CCutsceneMgr_Initialise);
    suppressVoid(syms_.CGangs_Initialise);
    suppressOnePtr(syms_.CAudioEngine_StartLoadingTune);
    suppressOnePtr(syms_.CAEGlobalWeaponAudioEntity_ServiceAmbientGunFire);
    if (syms_.CAEPedSpeechAudioEntity_AddSayEvent) {
        ArmHook::Install(
            reinterpret_cast<void*>(syms_.CAEPedSpeechAudioEntity_AddSayEvent),
            reinterpret_cast<void*>(&Hook_NoOpSpeech),
            nullptr);
    }

    BindClientEvents();

    hooksInstalled_ = true;
    WORLD_LOGI("GTA SA 2.00 + AML 1.4.0 hooks initialized cleanly (%s)",
               AmlCompat::Instance().GetSummaryText().c_str());
    return true;
}

void GtaSaWorld::BindClientEvents() {
    nana::net::SampClient::Events ev{};

    ev.onStateChanged = [this](nana::net::ConnectionState st, const std::string& msg) {
        if (st == nana::net::ConnectionState::DISCONNECTED) {
            CleanupAllRemoteEntities();
        }
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onStateChanged) cb.onStateChanged(static_cast<int>(st), msg);
    };

    ev.onChatMessage = [this](uint32_t rgba, const std::string& text) {
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onChatMessage) cb.onChatMessage(rgba, text);
    };

    ev.onClassSelection = [this](uint16_t classId, const nana::net::SpawnInfo& info) {
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onClassSelection) cb.onClassSelection(true, static_cast<int>(classId), static_cast<int>(info.skinId));
    };

    ev.onLocalSpawn = [this](const nana::net::SpawnInfo& info) {
        localDeadReported_ = false;
        ApplyLocalSpawn(info);
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onClassSelection) cb.onClassSelection(false, 0, static_cast<int>(info.skinId));
    };

    ev.onShowDialog = [this](const nana::net::DialogData& dlg) {
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onShowDialog) cb.onShowDialog(dlg);
    };

    ev.onHideDialog = [this]() {
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onHideDialog) cb.onHideDialog();
    };

    ev.onScoreboardUpdated = [this](const std::vector<nana::net::ScoreboardEntry>& entries) {
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cachedScoreboard_ = entries;
            cb = callbacks_;
        }
        if (cb.onScoreboardUpdated) cb.onScoreboardUpdated(entries);
    };

    ev.onGameText = [this](uint32_t style, uint32_t timeMs, const std::string& text) {
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onGameText) cb.onGameText(style, timeMs, text);
    };

    ev.onAudioStream = [this](const nana::net::AudioStreamState& audio) {
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onAudioStream) cb.onAudioStream(audio);
    };

    ev.onTextDrawsChanged = [this]() {
        std::vector<nana::net::TextDrawData> list;
        for (const auto& [id, td] : client_.GetTextDraws()) {
            list.push_back(td);
        }
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cachedTextDraws_ = std::move(list);
            cb = callbacks_;
        }
        if (cb.onTextDrawsChanged) cb.onTextDrawsChanged();
    };

    ev.onTextDrawSelectChanged = [this](const nana::net::TextDrawSelectState& sel) {
        UiEventCallbacks cb;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            cb = callbacks_;
        }
        if (cb.onTextDrawSelectChanged) cb.onTextDrawSelectChanged(sel.selecting, sel.hoverColorRgba);
    };

    ev.onRemotePlayerStreamIn = [this](const nana::net::RemotePlayerState& p) { SpawnOrUpdateRemotePed(p); };
    ev.onRemotePlayerStreamOut = [this](uint16_t pid) { DespawnRemotePed(pid); };
    ev.onRemotePlayerSync = [this](const nana::net::RemotePlayerState& p) { SpawnOrUpdateRemotePed(p); };

    ev.onRemoteVehicleStreamIn = [this](const nana::net::RemoteVehicleState& v) { SpawnOrUpdateRemoteVehicle(v); };
    ev.onRemoteVehicleStreamOut = [this](uint16_t vid) { DespawnRemoteVehicle(vid); };
    ev.onRemoteVehicleSync = [this](const nana::net::RemoteVehicleState& v) { SpawnOrUpdateRemoteVehicle(v); };

    ev.onObjectCreated = [this](const nana::net::WorldObjectState& o) { SpawnOrUpdateWorldObject(o); };
    ev.onObjectUpdated = [this](const nana::net::WorldObjectState& o) { SpawnOrUpdateWorldObject(o); };
    ev.onObjectDestroyed = [this](uint16_t oid) { DespawnWorldObject(oid); };

    ev.onCheckpointChanged = [this](const nana::net::CheckpointState& cp) {
        if (activeCheckpointBlip_ != 0 && syms_.CRadar_ClearBlip) {
            syms_.CRadar_ClearBlip(activeCheckpointBlip_);
            activeCheckpointBlip_ = 0;
        }
        if (cp.active && syms_.CRadar_SetShortRangeCoordBlip) {
            GtaVector v{cp.position.x, cp.position.y, cp.position.z};
            activeCheckpointBlip_ = syms_.CRadar_SetShortRangeCoordBlip(4, v, 0, 2, nullptr);
        }
    };

    ev.onSetLocalPos = [this](nana::net::Vec3 pos, bool findZ) {
        void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
        if (!ped || !syms_.CPed_Teleport) return;
        float z = pos.z;
        if (findZ && syms_.CWorld_FindGroundZForCoord) {
            float gz = syms_.CWorld_FindGroundZForCoord(pos.x, pos.y);
            if (gz > -90.0f) z = gz + 0.8f;
        }
        syms_.CPed_Teleport(ped, GtaVector{pos.x, pos.y, z}, 0);
    };

    ev.onSetLocalFacingAngle = [this](float angleDeg) {
        void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
        if (!ped) return;
        float rad = angleDeg * kDegToRad;
        PedCurrentRotation(ped) = rad;
        PedAimingRotation(ped) = rad;
        if (GtaMatrix* mat = EntityGetMatrix(ped)) {
            SetMatrixZHeading(mat, angleDeg);
        }
    };

    ev.onSetLocalHealth = [this](float hp) {
        void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
        if (ped) {
            PedHealth(ped) = std::clamp(hp, 0.0f, 255.0f);
            if (hp > 0.0f) localDeadReported_ = false;
        }
    };

    ev.onSetLocalArmour = [this](float arm) {
        void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
        if (ped) PedArmour(ped) = std::clamp(arm, 0.0f, 255.0f);
    };

    ev.onSetLocalSkin = [this](uint32_t skinId) {
        void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
        if (ped && syms_.CPed_SetModelIndex && EnsureModelLoaded(static_cast<int>(skinId))) {
            syms_.CPed_SetModelIndex(ped, skinId);
        }
    };

    ev.onSetLocalInterior = [this](uint8_t interiorId) {
        if (syms_.gCurrArea) *syms_.gCurrArea = static_cast<int>(interiorId);
    };

    ev.onSetLocalVelocity = [this](nana::net::Vec3 vel) {
        void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
        if (ped) PhysicalMoveSpeed(ped) = GtaVector{vel.x, vel.y, vel.z};
    };

    ev.onGiveLocalWeapon = [this](uint32_t weapId, uint32_t ammo) {
        void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
        if (ped && syms_.CPed_GiveWeapon) {
            syms_.CPed_GiveWeapon(ped, static_cast<int>(weapId), ammo, true);
        }
    };

    ev.onResetLocalWeapons = [this]() {
        void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
        if (ped && syms_.CPed_ClearWeapons) syms_.CPed_ClearWeapons(ped);
    };

    ev.onSetCameraPos = [this](nana::net::Vec3 pos) {
        pendingCamPos_ = pos;
        hasPendingCamPos_ = true;
        if (syms_.gTheCamera && syms_.CCamera_SetCamPositionForFixedMode) {
            GtaVector p{pos.x, pos.y, pos.z};
            GtaVector zero{0.0f, 0.0f, 0.0f};
            syms_.CCamera_SetCamPositionForFixedMode(syms_.gTheCamera, p, zero);
        }
    };

    ev.onSetCameraLookAt = [this](nana::net::Vec3 lookAt, uint8_t cutType) {
        if (syms_.gTheCamera && syms_.CCamera_TakeControlNoEntity) {
            GtaVector target{lookAt.x, lookAt.y, lookAt.z};
            syms_.CCamera_TakeControlNoEntity(syms_.gTheCamera, target, static_cast<int16_t>(cutType), 1);
        }
    };

    ev.onSetCameraBehindPlayer = [this]() {
        hasPendingCamPos_ = false;
        if (syms_.gTheCamera && syms_.CCamera_RestoreWithJumpCut) {
            syms_.CCamera_RestoreWithJumpCut(syms_.gTheCamera);
        }
    };

    ev.onSetTime = [this](uint8_t hour, uint8_t minute) {
        if (syms_.CClock_SetGameClock) syms_.CClock_SetGameClock(hour, minute, 0);
    };

    ev.onSetWeather = [this](uint8_t weatherId) {
        if (syms_.CWeather_ForceWeatherNow) syms_.CWeather_ForceWeatherNow(static_cast<int16_t>(weatherId));
    };

    client_.SetEvents(std::move(ev));
}

// ---- Thread-safe UI command enqueue helpers ----

void GtaSaWorld::ConnectAsync(const std::string& host, uint16_t port,
                              const std::string& nickname, const std::string& password) {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this, host, port, nickname, password]() {
        CleanupAllRemoteEntities();
        client_.Connect(host, port, nickname, password, nana::net::ProtocolVersion::SAMP_037);
    });
}

void GtaSaWorld::DisconnectAsync() {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this]() {
        client_.Disconnect();
        CleanupAllRemoteEntities();
    });
}

void GtaSaWorld::ReconnectAsync() {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this]() {
        CleanupAllRemoteEntities();
        client_.Reconnect();
    });
}

void GtaSaWorld::SendChatAsync(const std::string& text) {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this, text]() { client_.SendChat(text); });
}

void GtaSaWorld::RequestClassDeltaAsync(int delta) {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this, delta]() { client_.RequestNextClass(delta); });
}

void GtaSaWorld::RequestSpawnAsync() {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this]() { client_.RequestSpawn(); });
}

void GtaSaWorld::SendDialogResponseAsync(int16_t dialogId, uint8_t button, int16_t listItem, const std::string& inputText) {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this, dialogId, button, listItem, inputText]() {
        client_.SendDialogResponse(dialogId, button, listItem, inputText);
    });
}

void GtaSaWorld::RequestScoreboardAsync() {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this]() { client_.RequestScoreboardUpdate(); });
}

void GtaSaWorld::SendClickPlayerAsync(uint16_t playerId) {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this, playerId]() { client_.SendClickPlayer(playerId, 0); });
}

void GtaSaWorld::SendClickTextDrawAsync(uint16_t textDrawId) {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this, textDrawId]() { client_.SendClickTextDraw(textDrawId); });
}

void GtaSaWorld::CancelTextDrawSelectAsync() {
    std::lock_guard<std::mutex> lock(cmdMutex_);
    pendingCommands_.push_back([this]() { client_.SendClickTextDraw(0xFFFF); });
}

void GtaSaWorld::DrainUiCommands() {
    std::vector<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> lock(cmdMutex_);
        batch.swap(pendingCommands_);
    }
    for (auto& fn : batch) {
        if (fn) fn();
    }
}

// ---- GTA SA 2.00 Hooks ----

void GtaSaWorld::OnInitImageListHook() {
    if (origInitImageList_) {
        origInitImageList_();
    }
    if (!syms_.CStreaming_AddImageToList || storageRoot_.empty()) return;

    // Auto-register Alyn SA-MP custom IMG archives if present in <storageRoot>/texdb/
    std::string sampImg = FileCompat::Instance().ResolveExistingRegularFile("", "TEXDB\\SAMP.IMG");
    if (!sampImg.empty()) {
        int slot = syms_.CStreaming_AddImageToList("TEXDB\\SAMP.IMG", true);
        WORLD_LOGI("Registered SA-MP archive TEXDB\\SAMP.IMG -> slot %d", slot);
    }
    std::string sampCol = FileCompat::Instance().ResolveExistingRegularFile("", "TEXDB\\SAMPCOL.IMG");
    if (!sampCol.empty()) {
        int slot = syms_.CStreaming_AddImageToList("TEXDB\\SAMPCOL.IMG", true);
        WORLD_LOGI("Registered SA-MP archive TEXDB\\SAMPCOL.IMG -> slot %d", slot);
    }
}

void GtaSaWorld::LoadSampTextureDbOnce() {
    if (sampTexDbLoaded_ || storageRoot_.empty()) return;
    sampTexDbLoaded_ = true;

    uint32_t sampMask = ScanTexDbFormats(storageRoot_, "samp");
    if (sampMask != TEXDB_NONE && syms_.TextureDatabaseRuntime_Load && syms_.TextureDatabaseRuntime_Register) {
        void* db = GpuCompat::Instance().OnTextureDatabaseLoad(
            "samp", false, static_cast<int>(GtaTextureFormat::AUTO));
        if (db != nullptr) {
            syms_.TextureDatabaseRuntime_Register(db);
            WORLD_LOGI("Registered SA-MP texture database 'texdb/samp' (%p)", db);
        }
    }
}

void GtaSaWorld::OnMobileMenuInitTitle(void* menu) {
    if (origMobileMenuInitTitle_) {
        origMobileMenuInitTitle_(menu);
    }
    if (!newGameTriggered_ && syms_.StartGameScreen_OnNewGameCheck) {
        newGameTriggered_ = true;
        WORLD_LOGI("Bypassing singleplayer title screen -> StartGameScreen::OnNewGameCheck()");
        syms_.StartGameScreen_OnNewGameCheck();
    }
}

uint32_t GtaSaWorld::OnMainMenuScreenUpdate(void* screen, float dt) {
    if (!newGameTriggered_ && syms_.StartGameScreen_OnNewGameCheck) {
        newGameTriggered_ = true;
        WORLD_LOGI("MainMenuScreen::Update -> auto-launching multiplayer world");
        syms_.StartGameScreen_OnNewGameCheck();
    }
    return origMainMenuScreenUpdate_ ? origMainMenuScreenUpdate_(screen, dt) : 0;
}

void GtaSaWorld::OnDoGameStateHook() {
    doGameStateTicks_.fetch_add(1, std::memory_order_relaxed);

    if (syms_.gGameState) {
        int st = *syms_.gGameState;
        if (st != lastLoggedGameState_) {
            lastLoggedGameState_ = st;
            WORLD_LOGI("gGameState -> %d", st);
        }
        // State 7 = Frontend menu idle; auto-advance into Game Load (8) if not yet triggered
        if (st == 7 && !newGameTriggered_) {
            newGameTriggered_ = true;
            if (syms_.StartGameScreen_OnNewGameCheck) {
                syms_.StartGameScreen_OnNewGameCheck();
            } else if (syms_.Menu_NewGame) {
                syms_.Menu_NewGame();
            } else {
                *syms_.gGameState = 8;
            }
        }
    }

    if (origDoGameState_) {
        origDoGameState_();
    }

    // Keep RakNet networking ticking even while world is still streaming in
    if (!worldReady_) {
        DrainUiCommands();
        client_.Tick();
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        UpdateSnapshotLocked();
    }
}

// ---- Entity & World Streaming ----

bool GtaSaWorld::EnsureModelLoaded(int modelId) {
    if (modelId < 0 || modelId >= 20000) return false;
    if (!syms_.CStreaming_RequestModel || !syms_.CStreaming_LoadAllRequestedModels) return false;
    syms_.CStreaming_RequestModel(modelId, 0x06);
    syms_.CStreaming_LoadAllRequestedModels(false);
    return true;
}

void* GtaSaWorld::AllocateVehicleByModel(int modelId) {
    if (!syms_.CVehicle_OperatorNew) return nullptr;
    void* mem = syms_.CVehicle_OperatorNew(0xA18);
    if (!mem) return nullptr;

    if (syms_.CModelInfo_IsBmxModel && syms_.CModelInfo_IsBmxModel(modelId) && syms_.CBmx_Ctor) {
        syms_.CBmx_Ctor(mem, modelId, 2);
    } else if (syms_.CModelInfo_IsQuadBikeModel && syms_.CModelInfo_IsQuadBikeModel(modelId) && syms_.CQuadBike_Ctor) {
        syms_.CQuadBike_Ctor(mem, modelId, 2);
    } else if (syms_.CModelInfo_IsBikeModel && syms_.CModelInfo_IsBikeModel(modelId) && syms_.CBike_Ctor) {
        syms_.CBike_Ctor(mem, modelId, 2);
    } else if (syms_.CModelInfo_IsBoatModel && syms_.CModelInfo_IsBoatModel(modelId) && syms_.CBoat_Ctor) {
        syms_.CBoat_Ctor(mem, modelId, 2);
    } else if (syms_.CModelInfo_IsHeliModel && syms_.CModelInfo_IsHeliModel(modelId) && syms_.CHeli_Ctor) {
        syms_.CHeli_Ctor(mem, modelId, 2);
    } else if (syms_.CModelInfo_IsPlaneModel && syms_.CModelInfo_IsPlaneModel(modelId) && syms_.CPlane_Ctor) {
        syms_.CPlane_Ctor(mem, modelId, 2);
    } else if (syms_.CModelInfo_IsTrailerModel && syms_.CModelInfo_IsTrailerModel(modelId) && syms_.CTrailer_Ctor) {
        syms_.CTrailer_Ctor(mem, modelId, 2);
    } else if (syms_.CAutomobile_Ctor) {
        syms_.CAutomobile_Ctor(mem, modelId, 2, 1);
    } else {
        if (syms_.CVehicle_OperatorDelete) syms_.CVehicle_OperatorDelete(mem);
        return nullptr;
    }
    return mem;
}

void GtaSaWorld::ApplyLocalSpawn(const nana::net::SpawnInfo& spawn) {
    void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
    if (!ped) return;

    if (syms_.CPed_SetModelIndex && spawn.skinId <= 311 && EnsureModelLoaded(static_cast<int>(spawn.skinId))) {
        syms_.CPed_SetModelIndex(ped, spawn.skinId);
    }
    if (syms_.CPed_Teleport) {
        syms_.CPed_Teleport(ped, GtaVector{spawn.position.x, spawn.position.y, spawn.position.z}, 0);
    }
    float rad = spawn.rotationDeg * kDegToRad;
    PedCurrentRotation(ped) = rad;
    PedAimingRotation(ped) = rad;
    if (GtaMatrix* mat = EntityGetMatrix(ped)) {
        SetMatrixZHeading(mat, spawn.rotationDeg);
    }
    PedHealth(ped) = 100.0f;

    if (syms_.CPed_ClearWeapons) syms_.CPed_ClearWeapons(ped);
    if (syms_.CPed_GiveWeapon) {
        for (int i = 0; i < 3; ++i) {
            if (spawn.weapons[i] > 0 && spawn.ammo[i] > 0) {
                syms_.CPed_GiveWeapon(ped, static_cast<int>(spawn.weapons[i]),
                                      static_cast<uint32_t>(spawn.ammo[i]), true);
            }
        }
    }
    if (syms_.gTheCamera && syms_.CCamera_RestoreWithJumpCut) {
        syms_.CCamera_RestoreWithJumpCut(syms_.gTheCamera);
    }
}

void GtaSaWorld::SpawnOrUpdateRemotePed(const nana::net::RemotePlayerState& st) {
    if (!worldReady_) return;
    auto& ent = remotePeds_[st.playerId];
    ent.targetPos = st.onFoot.position;
    ent.velocity = st.onFoot.velocity;
    ent.targetAngleDeg = st.onFoot.quaternion.ToHeadingDegrees();

    if (!ent.pedPtr) {
        if (!syms_.CPed_OperatorNew || !syms_.CCivilianPed_Ctor || !syms_.CWorld_Add) return;
        uint32_t skin = (st.skinId <= 311) ? st.skinId : 0;
        if (!EnsureModelLoaded(static_cast<int>(skin))) return;

        void* mem = syms_.CPed_OperatorNew(0x7A4);
        if (!mem) return;
        syms_.CCivilianPed_Ctor(mem, 4 /* PED_TYPE_CIVMALE */, skin);
        if (syms_.CPed_Teleport) {
            syms_.CPed_Teleport(mem, GtaVector{ent.targetPos.x, ent.targetPos.y, ent.targetPos.z}, 0);
        }
        syms_.CWorld_Add(mem);
        ent.pedPtr = mem;
        ent.modelId = skin;
        ent.spawned = true;
    }

    if (ent.pedPtr && st.onFoot.weapon != ent.currentWeapon && syms_.CPed_GiveWeapon) {
        ent.currentWeapon = st.onFoot.weapon;
        if (ent.currentWeapon > 0) {
            syms_.CPed_GiveWeapon(ent.pedPtr, ent.currentWeapon, 9999, true);
        }
    }
}

void GtaSaWorld::DespawnRemotePed(uint16_t playerId) {
    auto it = remotePeds_.find(playerId);
    if (it == remotePeds_.end()) return;
    if (it->second.pedPtr) {
        if (syms_.CWorld_Remove) syms_.CWorld_Remove(it->second.pedPtr);
        if (syms_.CPed_OperatorDelete) syms_.CPed_OperatorDelete(it->second.pedPtr);
    }
    remotePeds_.erase(it);
}

void GtaSaWorld::SpawnOrUpdateRemoteVehicle(const nana::net::RemoteVehicleState& st) {
    if (!worldReady_) return;
    auto& ent = remoteVehicles_[st.vehicleId];
    ent.targetPos = st.position;
    ent.velocity = st.velocity;
    ent.targetAngleDeg = st.rotationDeg;
    ent.health = st.health;

    if (!ent.vehPtr) {
        if (!syms_.CWorld_Add) return;
        int model = static_cast<int>(st.modelId);
        if (model < 400 || model > 611) model = 411;
        if (!EnsureModelLoaded(model)) return;

        void* veh = AllocateVehicleByModel(model);
        if (!veh) return;
        if (syms_.CEntity_Teleport) {
            syms_.CEntity_Teleport(veh, GtaVector{ent.targetPos.x, ent.targetPos.y, ent.targetPos.z}, 0);
        }
        if (GtaMatrix* mat = EntityGetMatrix(veh)) {
            mat->pos = GtaVector{ent.targetPos.x, ent.targetPos.y, ent.targetPos.z};
            SetMatrixZHeading(mat, ent.targetAngleDeg);
        }
        VehicleHealth(veh) = ent.health;
        syms_.CWorld_Add(veh);
        ent.vehPtr = veh;
        ent.modelId = static_cast<uint32_t>(model);
        ent.spawned = true;
    }
}

void GtaSaWorld::DespawnRemoteVehicle(uint16_t vehicleId) {
    auto it = remoteVehicles_.find(vehicleId);
    if (it == remoteVehicles_.end()) return;
    if (it->second.vehPtr) {
        if (syms_.CWorld_Remove) syms_.CWorld_Remove(it->second.vehPtr);
        if (syms_.CVehicle_OperatorDelete) syms_.CVehicle_OperatorDelete(it->second.vehPtr);
    }
    remoteVehicles_.erase(it);
}

void GtaSaWorld::SpawnOrUpdateWorldObject(const nana::net::WorldObjectState& obj) {
    if (!worldReady_ || !syms_.CObject_Create || !syms_.CWorld_Add) return;
    auto& ent = remoteObjects_[obj.objectId];
    if (!ent.objPtr) {
        int model = static_cast<int>(obj.modelId);
        if (!EnsureModelLoaded(model)) return;
        void* ptr = syms_.CObject_Create(model, false);
        if (!ptr) return;
        if (GtaMatrix* mat = EntityGetMatrix(ptr)) {
            mat->pos = GtaVector{obj.position.x, obj.position.y, obj.position.z};
            SetMatrixZHeading(mat, obj.rotation.z);
        }
        syms_.CWorld_Add(ptr);
        ent.objPtr = ptr;
        ent.modelId = obj.modelId;
    } else if (GtaMatrix* mat = EntityGetMatrix(ent.objPtr)) {
        mat->pos = GtaVector{obj.position.x, obj.position.y, obj.position.z};
        SetMatrixZHeading(mat, obj.rotation.z);
    }
}

void GtaSaWorld::DespawnWorldObject(uint16_t objectId) {
    auto it = remoteObjects_.find(objectId);
    if (it == remoteObjects_.end()) return;
    if (it->second.objPtr && syms_.CWorld_Remove) {
        syms_.CWorld_Remove(it->second.objPtr);
    }
    remoteObjects_.erase(it);
}

void GtaSaWorld::CleanupAllRemoteEntities() {
    for (auto& [pid, ent] : remotePeds_) {
        if (ent.pedPtr) {
            if (syms_.CWorld_Remove) syms_.CWorld_Remove(ent.pedPtr);
            if (syms_.CPed_OperatorDelete) syms_.CPed_OperatorDelete(ent.pedPtr);
        }
    }
    remotePeds_.clear();

    for (auto& [vid, ent] : remoteVehicles_) {
        if (ent.vehPtr) {
            if (syms_.CWorld_Remove) syms_.CWorld_Remove(ent.vehPtr);
            if (syms_.CVehicle_OperatorDelete) syms_.CVehicle_OperatorDelete(ent.vehPtr);
        }
    }
    remoteVehicles_.clear();

    for (auto& [oid, ent] : remoteObjects_) {
        if (ent.objPtr && syms_.CWorld_Remove) {
            syms_.CWorld_Remove(ent.objPtr);
        }
    }
    remoteObjects_.clear();

    if (activeCheckpointBlip_ != 0 && syms_.CRadar_ClearBlip) {
        syms_.CRadar_ClearBlip(activeCheckpointBlip_);
        activeCheckpointBlip_ = 0;
    }
}

void GtaSaWorld::InterpolateRemoteEntities() {
    constexpr float kAlpha = 0.28f;

    for (auto& [pid, ent] : remotePeds_) {
        if (!ent.pedPtr) continue;
        GtaMatrix* mat = EntityGetMatrix(ent.pedPtr);
        if (!mat) continue;
        float dx = ent.targetPos.x - mat->pos.x;
        float dy = ent.targetPos.y - mat->pos.y;
        float dz = ent.targetPos.z - mat->pos.z;
        if (dx * dx + dy * dy + dz * dz > 100.0f) {
            mat->pos = GtaVector{ent.targetPos.x, ent.targetPos.y, ent.targetPos.z};
        } else {
            mat->pos.x += dx * kAlpha;
            mat->pos.y += dy * kAlpha;
            mat->pos.z += dz * kAlpha;
        }
        SetMatrixZHeading(mat, ent.targetAngleDeg);
        PedCurrentRotation(ent.pedPtr) = ent.targetAngleDeg * kDegToRad;
        PhysicalMoveSpeed(ent.pedPtr) = GtaVector{ent.velocity.x, ent.velocity.y, ent.velocity.z};
    }

    for (auto& [vid, ent] : remoteVehicles_) {
        if (!ent.vehPtr) continue;
        GtaMatrix* mat = EntityGetMatrix(ent.vehPtr);
        if (!mat) continue;
        float dx = ent.targetPos.x - mat->pos.x;
        float dy = ent.targetPos.y - mat->pos.y;
        float dz = ent.targetPos.z - mat->pos.z;
        if (dx * dx + dy * dy + dz * dz > 225.0f) {
            mat->pos = GtaVector{ent.targetPos.x, ent.targetPos.y, ent.targetPos.z};
        } else {
            mat->pos.x += dx * kAlpha;
            mat->pos.y += dy * kAlpha;
            mat->pos.z += dz * kAlpha;
        }
        SetMatrixZHeading(mat, ent.targetAngleDeg);
        PhysicalMoveSpeed(ent.vehPtr) = GtaVector{ent.velocity.x, ent.velocity.y, ent.velocity.z};
        VehicleHealth(ent.vehPtr) = ent.health;
    }
}

void GtaSaWorld::ReadAndPushLocalPlayerSync() {
    void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
    if (!ped) return;

    float hp = std::clamp(PedHealth(ped), 0.0f, 255.0f);
    float arm = std::clamp(PedArmour(ped), 0.0f, 255.0f);

    // Death system: detect transition to <= 0 HP and send RPC_Death
    if (hp <= 0.0f && !localDeadReported_) {
        localDeadReported_ = true;
        client_.SendDeath(255 /* suicide/generic */, nana::net::INVALID_PLAYER_ID);
    }

    void* pad = syms_.CPad_GetPad ? syms_.CPad_GetPad(0) : nullptr;

    // Camera synchronization (ID_AIM_SYNC): extract active CCam vectors from TheCamera
    if (syms_.gTheCamera) {
        auto* camBase = static_cast<uint8_t*>(syms_.gTheCamera);
        uint8_t activeIdx = *(camBase + GtaOffsets::CAMERA_ACTIVE_CAM_IDX);
        if (activeIdx < 3) {
            uint8_t* camPtr = camBase + GtaOffsets::CAMERA_CAMS_ARRAY + activeIdx * GtaOffsets::CAM_STRUCT_STRIDE;
            uint16_t camMode = *reinterpret_cast<uint16_t*>(camPtr + GtaOffsets::CAM_MODE);
            GtaVector front = *reinterpret_cast<GtaVector*>(camPtr + GtaOffsets::CAM_FRONT);
            GtaVector src = *reinterpret_cast<GtaVector*>(camPtr + GtaOffsets::CAM_SOURCE);
            if (std::isfinite(front.x) && std::isfinite(src.x)) {
                nana::net::AimSyncData aim{};
                aim.camMode = static_cast<uint8_t>(camMode & 0xFFu);
                aim.camFront = nana::net::Vec3{front.x, front.y, front.z};
                aim.camPos = nana::net::Vec3{src.x, src.y, src.z};
                aim.aimZ = 0.0f;
                aim.camExtZoomAndWeaponState = 0x3F;
                aim.aspectRatio = 0x55;
                client_.UpdateLocalAim(aim);
            }
        }
    }

    if (!PedIsInVehicle(ped)) {
        client_.SetLocalVehicleState(false, false);
        GtaMatrix* mat = EntityGetMatrix(ped);
        if (!mat) return;

        nana::net::OnFootSyncData sync{};
        sync.position = nana::net::Vec3{mat->pos.x, mat->pos.y, mat->pos.z};
        GtaVector vel = PhysicalMoveSpeed(ped);
        sync.velocity = nana::net::Vec3{vel.x, vel.y, vel.z};
        float angleDeg = PedCurrentRotation(ped) * kRadToDeg;
        sync.quaternion = nana::net::Quat::FromHeadingDegrees(angleDeg);
        sync.health = static_cast<uint8_t>(hp);
        sync.armour = static_cast<uint8_t>(arm);
        sync.weapon = PedGetCurrentWeaponId(ped);

        if (pad) {
            if (syms_.CPad_GetPedWalkLeftRight) {
                sync.leftRight = static_cast<uint16_t>(syms_.CPad_GetPedWalkLeftRight(pad));
            }
            if (syms_.CPad_GetPedWalkUpDown) {
                sync.upDown = static_cast<uint16_t>(syms_.CPad_GetPedWalkUpDown(pad));
            }
            sync.keys = ReadLocalPadKeys(syms_, pad, false);
        }

        client_.UpdateLocalOnFoot(sync);
    } else {
        void* vehPtr = PedGetVehicle(ped);
        GtaMatrix* vmat = EntityGetMatrix(vehPtr);
        if (vehPtr && vmat) {
            uint16_t matchedVehId = 0;
            for (const auto& [vid, ent] : remoteVehicles_) {
                if (ent.vehPtr == vehPtr) {
                    matchedVehId = vid;
                    break;
                }
            }
            if (matchedVehId > 0) {
                bool isDriver = (VehicleGetDriver(vehPtr) == ped);
                client_.SetLocalVehicleState(true, !isDriver);

                if (isDriver) {
                    nana::net::InCarSyncData carSync{};
                    carSync.vehicleId = matchedVehId;
                    carSync.position = nana::net::Vec3{vmat->pos.x, vmat->pos.y, vmat->pos.z};
                    GtaVector vel = PhysicalMoveSpeed(vehPtr);
                    carSync.velocity = nana::net::Vec3{vel.x, vel.y, vel.z};
                    carSync.carHealth = VehicleHealth(vehPtr);
                    carSync.playerHealth = static_cast<uint8_t>(hp);
                    carSync.playerArmour = static_cast<uint8_t>(arm);
                    carSync.weapon = PedGetCurrentWeaponId(ped);
                    if (pad) {
                        if (syms_.CPad_GetSteeringLeftRight) {
                            carSync.leftRight = static_cast<uint16_t>(syms_.CPad_GetSteeringLeftRight(pad));
                        }
                        carSync.keys = ReadLocalPadKeys(syms_, pad, true);
                    }
                    float yawDeg = std::atan2(-vmat->up.x, vmat->up.y) * kRadToDeg;
                    carSync.quaternion = nana::net::Quat::FromHeadingDegrees(yawDeg);
                    client_.UpdateLocalInCar(carSync);
                } else {
                    nana::net::PassengerSyncData pSync{};
                    pSync.vehicleId = matchedVehId;
                    pSync.seatFlags = 1;
                    pSync.weapon = PedGetCurrentWeaponId(ped);
                    pSync.health = static_cast<uint8_t>(hp);
                    pSync.armour = static_cast<uint8_t>(arm);
                    pSync.position = nana::net::Vec3{vmat->pos.x, vmat->pos.y, vmat->pos.z};
                    client_.UpdateLocalPassenger(pSync);
                }
            }
        }
    }
}

void GtaSaWorld::UpdateSnapshotLocked() {
    snapshot_.connectionState = static_cast<int>(client_.GetState());
    snapshot_.statusMessage = client_.GetStatusMessage();
    snapshot_.serverName = client_.GetServerSettings().serverName;
    snapshot_.localPlayerId = (client_.GetLocalPlayerId() == nana::net::INVALID_PLAYER_ID)
                                  ? -1
                                  : static_cast<int>(client_.GetLocalPlayerId());
    snapshot_.pingMs = static_cast<int>(client_.GetPingMs());
    snapshot_.playerCount = static_cast<int>(client_.GetConnectedPlayerCount());
    snapshot_.score = client_.GetLocalScore();
    snapshot_.money = client_.GetLocalMoney();
    snapshot_.inClassSelection = client_.IsInClassSelection();
    snapshot_.currentClassId = static_cast<int>(client_.GetCurrentClassId());
    snapshot_.gameWorldReady = worldReady_;

    auto aml = AmlCompat::Instance().GetStatus();
    snapshot_.amlLoaded = aml.libraryLoaded;
    snapshot_.amlModCount = static_cast<int>(aml.discoveredMods.size());
    snapshot_.amlSummary = AmlCompat::Instance().GetSummaryText();

    void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
    if (ped) {
        snapshot_.localHealth = std::clamp(PedHealth(ped), 0.0f, 255.0f);
        snapshot_.localArmour = std::clamp(PedArmour(ped), 0.0f, 255.0f);
        if (GtaMatrix* mat = EntityGetMatrix(ped)) {
            snapshot_.localX = mat->pos.x;
            snapshot_.localY = mat->pos.y;
            snapshot_.localZ = mat->pos.z;
        }
    }
}

ClientUiSnapshot GtaSaWorld::GetSnapshot() const {
    ClientUiSnapshot out;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        out = snapshot_;
    }
    out.gameState = syms_.gGameState ? *syms_.gGameState : -1;
    if (syms_.gLoadingPercent) {
        float pct = *syms_.gLoadingPercent;
        if (std::isfinite(pct)) {
            out.loadingPercent = static_cast<int>(std::clamp(pct, 0.0f, 100.0f));
        }
    }
    out.texDbLoadedCount = GpuCompat::Instance().GetLoadedDbCount();
    out.doGameStateTicks = doGameStateTicks_.load(std::memory_order_relaxed);
    return out;
}

std::vector<nana::net::TextDrawData> GtaSaWorld::GetTextDrawsSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return cachedTextDraws_;
}

std::vector<nana::net::ScoreboardEntry> GtaSaWorld::GetScoreboardSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return cachedScoreboard_;
}

void GtaSaWorld::OnGameProcessFrame() {
    if (origCGameProcess_) {
        origCGameProcess_();
    }

    void* ped = syms_.FindPlayerPed ? syms_.FindPlayerPed(-1) : nullptr;
    if (ped != nullptr && !worldReady_) {
        worldReady_ = true;
        LoadSampTextureDbOnce();
        WORLD_LOGI("GTA SA 2.00 gameplay world ready (local CPlayerPed=%p)", ped);
    }

    DrainUiCommands();

    if (worldReady_ && client_.IsSpawned()) {
        ReadAndPushLocalPlayerSync();
    }

    client_.Tick();

    if (worldReady_) {
        InterpolateRemoteEntities();
    }

    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        UpdateSnapshotLocked();
    }
}

} // namespace nana::game
