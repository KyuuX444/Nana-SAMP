#pragma once

#include "gtasa_symbols.h"
#include "../net/samp_client.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace nana::game {

struct ClientUiSnapshot {
    int connectionState = 0;
    std::string statusMessage;
    std::string serverName;
    int localPlayerId = -1;
    int pingMs = 0;
    int playerCount = 0;
    int score = 0;
    int money = 0;
    bool inClassSelection = false;
    int currentClassId = 0;
    bool gameWorldReady = false;
    int gameState = -1;
    int loadingPercent = 0;
    int texDbLoadedCount = 0;
    int doGameStateTicks = 0;
    bool amlLoaded = false;
    int amlModCount = 0;
    std::string amlSummary;
    float localHealth = 100.0f;
    float localArmour = 0.0f;
    float localX = 0.0f;
    float localY = 0.0f;
    float localZ = 0.0f;
};

struct UiEventCallbacks {
    std::function<void(int state, const std::string& message)> onStateChanged;
    std::function<void(uint32_t rgba, const std::string& text)> onChatMessage;
    std::function<void(bool visible, int classId, int skinId)> onClassSelection;
    std::function<void(const nana::net::DialogData& dialog)> onShowDialog;
    std::function<void()> onHideDialog;
    std::function<void(const std::vector<nana::net::ScoreboardEntry>& entries)> onScoreboardUpdated;
    std::function<void(uint32_t style, uint32_t timeMs, const std::string& text)> onGameText;
    std::function<void(const nana::net::AudioStreamState& audio)> onAudioStream;
    std::function<void()> onTextDrawsChanged;
    std::function<void(bool enabled, uint32_t hoverColor)> onTextDrawSelectChanged;
};

/**
 * Native GTA SA Android 2.00 ARM32 multiplayer integration controller.
 *
 * Bridges SampClient (RakNet / SA-MP 0.3.7 & 0.3.DL / open.mp) with libGTASA.so 2.00 on the
 * GTA SA game thread via verified Thumb-2 hooks:
 *   - CGame::Process: drives networking, entity streaming, interpolation, local on-foot/in-car/camera sync, death detection
 *   - DoGameState + MainMenuScreen::Update + MobileMenu::InitForTitle: bypasses singleplayer menu cleanly
 *   - CStreaming::InitImageList: auto-registers Alyn SA-MP TEXDB\SAMP.IMG & TEXDB\SAMPCOL.IMG + texdb/samp
 *   - NvFOpen (FileCompat) & TextureDatabaseRuntime::Load (GpuCompat): Alyn SA-MP overrides + Mali/Adreno/PVR compat
 *   - AML 1.4.0 (AmlCompat): preserves Android Mod Loader 1.4.0 cleanly
 */
class GtaSaWorld {
public:
    static GtaSaWorld& Instance();

    void SetCallbacks(UiEventCallbacks cb);

    bool InitializeHooks(const std::string& storageRoot,
                         int preferredTexFormat,
                         const std::string& gpuRenderer,
                         const std::string& gpuExtensions);

    // Commands invoked safely from Android UI thread (queued to game thread)
    void ConnectAsync(const std::string& host, uint16_t port,
                      const std::string& nickname, const std::string& password);
    void DisconnectAsync();
    void ReconnectAsync();
    void SendChatAsync(const std::string& text);
    void RequestClassDeltaAsync(int delta);
    void RequestSpawnAsync();
    void SendDialogResponseAsync(int16_t dialogId, uint8_t button, int16_t listItem, const std::string& inputText);
    void RequestScoreboardAsync();
    void SendClickPlayerAsync(uint16_t playerId);
    void SendClickTextDrawAsync(uint16_t textDrawId);
    void CancelTextDrawSelectAsync();

    // Thread-safe snapshots for Android HUD / Overlay UI
    ClientUiSnapshot GetSnapshot() const;
    std::vector<nana::net::TextDrawData> GetTextDrawsSnapshot() const;
    std::vector<nana::net::ScoreboardEntry> GetScoreboardSnapshot() const;

    // Hook callbacks invoked on the GTA SA game thread
    void OnDoGameStateHook();
    void OnGameProcessFrame();
    void OnInitImageListHook();
    void OnMobileMenuInitTitle(void* menu);
    uint32_t OnMainMenuScreenUpdate(void* screen, float dt);

private:
    GtaSaWorld() = default;

    struct RemotePedEntity {
        void* pedPtr = nullptr;
        uint32_t modelId = 0;
        nana::net::Vec3 targetPos{};
        nana::net::Vec3 velocity{};
        float targetAngleDeg = 0.0f;
        uint8_t currentWeapon = 0;
        bool spawned = false;
    };

    struct RemoteVehicleEntity {
        void* vehPtr = nullptr;
        uint32_t modelId = 411;
        nana::net::Vec3 targetPos{};
        nana::net::Vec3 velocity{};
        float targetAngleDeg = 0.0f;
        float health = 1000.0f;
        bool spawned = false;
    };

    struct RemoteObjectEntity {
        void* objPtr = nullptr;
        uint32_t modelId = 0;
    };

    void BindClientEvents();
    void DrainUiCommands();
    void CleanupAllRemoteEntities();
    void ReadAndPushLocalPlayerSync();
    void InterpolateRemoteEntities();
    void UpdateSnapshotLocked();
    void LoadSampTextureDbOnce();

    void SpawnOrUpdateRemotePed(const nana::net::RemotePlayerState& state);
    void DespawnRemotePed(uint16_t playerId);
    void SpawnOrUpdateRemoteVehicle(const nana::net::RemoteVehicleState& state);
    void DespawnRemoteVehicle(uint16_t vehicleId);
    void SpawnOrUpdateWorldObject(const nana::net::WorldObjectState& obj);
    void DespawnWorldObject(uint16_t objectId);
    void ApplyLocalSpawn(const nana::net::SpawnInfo& spawn);

    bool EnsureModelLoaded(int modelId);
    void* AllocateVehicleByModel(int modelId);

    GtaSymbols syms_{};
    nana::net::SampClient client_{};
    UiEventCallbacks callbacks_{};
    std::string storageRoot_;

    bool hooksInstalled_ = false;
    bool worldReady_ = false;
    bool newGameTriggered_ = false;
    bool sampTexDbLoaded_ = false;
    bool localDeadReported_ = false;
    int activeCheckpointBlip_ = 0;
    nana::net::Vec3 pendingCamPos_{};
    bool hasPendingCamPos_ = false;

    std::atomic<int> doGameStateTicks_{0};
    int lastLoggedGameState_ = -999;

    // Original trampoline pointers populated by ArmHook::Install
    void (*origDoGameState_)() = nullptr;
    void (*origCGameProcess_)() = nullptr;
    void (*origInitImageList_)() = nullptr;
    void (*origMobileMenuInitTitle_)(void* menu) = nullptr;
    uint32_t (*origMainMenuScreenUpdate_)(void* screen, float dt) = nullptr;

    std::map<uint16_t, RemotePedEntity> remotePeds_;
    std::map<uint16_t, RemoteVehicleEntity> remoteVehicles_;
    std::map<uint16_t, RemoteObjectEntity> remoteObjects_;

    mutable std::mutex cmdMutex_;
    std::vector<std::function<void()>> pendingCommands_;

    mutable std::mutex snapshotMutex_;
    ClientUiSnapshot snapshot_{};
    std::vector<nana::net::TextDrawData> cachedTextDraws_;
    std::vector<nana::net::ScoreboardEntry> cachedScoreboard_;
};

} // namespace nana::game
