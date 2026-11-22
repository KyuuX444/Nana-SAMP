#pragma once

#include "raknet_peer.h"
#include "samp_protocol.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nana::net {

/**
 * Complete clean-room SA-MP 0.3.7 & 0.3.DL-R1 / open.mp multiplayer client state machine.
 *
 * Implements:
 * - Cookie handshake, ID_AUTH_KEY, RPC_ClientJoin (0.3.7=4057 & 0.3.DL-R1=4062)
 * - 0.3.DL-R1 download completion handshake (RPC_FinishedDownloading = 185)
 * - RPC_InitGame, RPC_ServerJoin/Quit, RPC_UpdateScoresPingsIPs (Scoreboard), RPC_ClickPlayer
 * - RPC_ShowDialog (RakNet Huffman StringCompressor) & RPC_DialogResponse
 * - Class selection & spawning (RPC_RequestClass, RPC_RequestSpawn, RPC_Spawn, RPC_ForceClassSelection)
 * - Real-time sync: ID_PLAYER_SYNC (207), ID_VEHICLE_SYNC (200), ID_PASSENGER_SYNC (211),
 *   ID_AIM_SYNC (203), ID_MARKERS_SYNC (208), ID_STATS_UPDATE (205)
 * - World RPCs: Players, Vehicles, Objects, RemovedBuildings, Pickups, Checkpoints,
 *   3D Text Labels, GameText, Audio Streams, Camera, Time, Weather, Gravity
 */
class SampClient {
public:
    struct Events {
        std::function<void(ConnectionState state, const std::string& statusMessage)> onStateChanged;
        std::function<void(uint32_t rgbaColor, const std::string& text)> onChatMessage;
        std::function<void(const ServerSettings& settings)> onInitGame;
        std::function<void(uint16_t classId, const SpawnInfo& spawnInfo)> onClassSelection;
        std::function<void(const SpawnInfo& spawnInfo)> onLocalSpawn;
        std::function<void(const DialogData& dialog)> onShowDialog;
        std::function<void()> onHideDialog;
        std::function<void(const std::vector<ScoreboardEntry>& entries)> onScoreboardUpdated;
        std::function<void(uint32_t style, uint32_t timeMs, const std::string& text)> onGameText;
        std::function<void(const AudioStreamState& audio)> onAudioStream;
        std::function<void()> onTextDrawsChanged;
        std::function<void(const TextDrawSelectState& sel)> onTextDrawSelectChanged;
        std::function<void(const RemotePlayerState& player)> onRemotePlayerStreamIn;
        std::function<void(uint16_t playerId)> onRemotePlayerStreamOut;
        std::function<void(const RemotePlayerState& player)> onRemotePlayerSync;
        std::function<void(const RemoteVehicleState& vehicle)> onRemoteVehicleStreamIn;
        std::function<void(uint16_t vehicleId)> onRemoteVehicleStreamOut;
        std::function<void(const RemoteVehicleState& vehicle)> onRemoteVehicleSync;
        std::function<void(const WorldObjectState& obj)> onObjectCreated;
        std::function<void(const WorldObjectState& obj)> onObjectUpdated;
        std::function<void(uint16_t objectId)> onObjectDestroyed;
        std::function<void(const RemovedBuildingRule& rule)> onRemoveBuilding;
        std::function<void(const CheckpointState& cp)> onCheckpointChanged;
        std::function<void(const PickupState& pickup)> onPickupCreated;
        std::function<void(int32_t pickupId)> onPickupDestroyed;
        std::function<void(const TextLabel3D& label)> onTextLabelCreated;
        std::function<void(uint16_t labelId)> onTextLabelDestroyed;
        std::function<void(Vec3 pos, bool findZ)> onSetLocalPos;
        std::function<void(float angleDeg)> onSetLocalFacingAngle;
        std::function<void(float health)> onSetLocalHealth;
        std::function<void(float armour)> onSetLocalArmour;
        std::function<void(uint32_t skinId)> onSetLocalSkin;
        std::function<void(uint8_t interiorId)> onSetLocalInterior;
        std::function<void(Vec3 vel)> onSetLocalVelocity;
        std::function<void(bool controllable)> onToggleControllable;
        std::function<void(uint32_t weaponId, uint32_t ammo)> onGiveLocalWeapon;
        std::function<void()> onResetLocalWeapons;
        std::function<void(int32_t moneyDelta, bool reset)> onMoneyChanged;
        std::function<void(float gravity)> onSetGravity;
        std::function<void(Vec3 pos)> onSetCameraPos;
        std::function<void(Vec3 lookAt, uint8_t cutType)> onSetCameraLookAt;
        std::function<void()> onSetCameraBehindPlayer;
        std::function<void(uint8_t hour, uint8_t minute)> onSetTime;
        std::function<void(uint8_t weatherId)> onSetWeather;
        std::function<void(uint16_t vehicleId, uint8_t seatId)> onPutLocalInVehicle;
        std::function<void()> onRemoveLocalFromVehicle;
        std::function<void(uint16_t playerId, const std::string& name, bool joined)> onPlayerConnectionChanged;
    };

    SampClient();
    ~SampClient();

    void SetEvents(Events events) { events_ = std::move(events); }

    bool Connect(const std::string& host, uint16_t port,
                 const std::string& nickname, const std::string& password = "",
                 ProtocolVersion protocol = ProtocolVersion::SAMP_037);
    void Disconnect();
    bool Reconnect();

    // Advance networking & periodic sync transmission.
    void Tick();

    // Gameplay actions from UI or engine
    void RequestClass(uint16_t classId);
    void RequestNextClass(int delta);
    void RequestSpawn();
    void SendChat(const std::string& text);
    void SendDialogResponse(int16_t dialogId, uint8_t button, int16_t listItem, const std::string& inputText);
    void RequestScoreboardUpdate();
    void SendClickPlayer(uint16_t playerId, uint8_t source = 0);
    void SendDeath(uint8_t reason, uint16_t killerId = INVALID_PLAYER_ID);
    void SendEnterVehicle(uint16_t vehicleId, bool passenger);
    void SendExitVehicle(uint16_t vehicleId);
    void SendClickTextDraw(uint16_t textDrawId);
    void SendFinishedDownloading();

    // Local player state updates pushed by engine or test harness
    void UpdateLocalOnFoot(const OnFootSyncData& data);
    void UpdateLocalInCar(const InCarSyncData& data);
    void UpdateLocalPassenger(const PassengerSyncData& data);
    void UpdateLocalAim(const AimSyncData& data);
    void SetLocalVehicleState(bool inVehicle, bool asPassenger = false) {
        localInVehicle_ = inVehicle;
        localIsPassenger_ = asPassenger;
    }

    // Inspection getters
    ConnectionState GetState() const { return state_; }
    ProtocolVersion GetProtocolVersion() const { return protocol_; }
    std::string GetStatusMessage() const { return statusMessage_; }
    uint16_t GetLocalPlayerId() const { return settings_.localPlayerId; }
    const std::string& GetNickname() const { return nickname_; }
    uint32_t GetPingMs() const { return peer_.GetPingMs(); }
    int32_t GetLocalScore() const { return localScore_; }
    int32_t GetLocalMoney() const { return localMoney_; }
    const ServerSettings& GetServerSettings() const { return settings_; }
    const SpawnInfo& GetCurrentSpawnInfo() const { return spawnInfo_; }
    uint16_t GetCurrentClassId() const { return currentClassId_; }
    bool IsInClassSelection() const { return inClassSelection_; }
    bool IsSpawned() const { return state_ == ConnectionState::SPAWNED; }
    size_t GetConnectedPlayerCount() const { return players_.size() + (state_ >= ConnectionState::CONNECTED ? 1 : 0); }

    std::vector<ScoreboardEntry> GetScoreboardEntries() const;
    const std::map<uint16_t, RemotePlayerState>& GetRemotePlayers() const { return players_; }
    const std::map<uint16_t, RemoteVehicleState>& GetRemoteVehicles() const { return vehicles_; }
    const std::map<uint16_t, WorldObjectState>& GetWorldObjects() const { return objects_; }
    const std::map<uint16_t, TextDrawData>& GetTextDraws() const { return textDraws_; }
    const TextDrawSelectState& GetTextDrawSelectState() const { return textDrawSelect_; }
    const DialogData& GetActiveDialog() const { return activeDialog_; }

    // Wire-format codecs exposed for deterministic unit tests
    static void WriteClientOnFootSync(BitStream& bs, const OnFootSyncData& data);
    static void WriteClientInCarSync(BitStream& bs, const InCarSyncData& data);
    static void WriteClientPassengerSync(BitStream& bs, const PassengerSyncData& data);
    static void WriteClientAimSync(BitStream& bs, const AimSyncData& data);
    static bool ReadServerOnFootSync(BitStream& bs, uint16_t& outPlayerId, OnFootSyncData& outData);
    static bool ReadServerInCarSync(BitStream& bs, uint16_t& outPlayerId, InCarSyncData& outData);
    static bool ReadServerPassengerSync(BitStream& bs, uint16_t& outPlayerId, PassengerSyncData& outData);
    static bool ReadServerShowDialog(BitStream& bs, DialogData& outDialog);
    static bool ReadServerShowTextDraw(BitStream& bs, TextDrawData& outTd);

private:
    static uint64_t NowMs();
    static uint8_t UnpackHealthArmourNibble(uint8_t nibble);

    void SetState(ConnectionState st, const std::string& message);
    void EmitChat(uint32_t rgba, const std::string& text);

    void OnPeerConnected(uint16_t playerIndex, uint32_t serverChallenge);
    void OnPeerDisconnected(uint8_t reasonPacketId, const std::string& reasonText);
    void OnRpcReceived(uint8_t rpcId, BitStream& bs);
    void OnPacketReceived(uint8_t packetId, BitStream& bs);

    void SendClientJoin(uint32_t serverChallenge);
    void SendSyncPacketsIfDue(uint64_t nowMs);

    void HandleRpcInitGame(BitStream& bs);
    void HandleRpcServerJoin(BitStream& bs);
    void HandleRpcServerQuit(BitStream& bs);
    void HandleRpcClientMessage(BitStream& bs);
    void HandleRpcChat(BitStream& bs);
    void HandleRpcShowDialog(BitStream& bs);
    void HandleRpcUpdateScoresPings(BitStream& bs);
    void HandleRpcRequestClass(BitStream& bs);
    void HandleRpcRequestSpawn(BitStream& bs);
    void HandleRpcSetSpawnInfo(BitStream& bs);
    void HandleRpcWorldPlayerAdd(BitStream& bs);
    void HandleRpcWorldPlayerRemove(BitStream& bs);
    void HandleRpcConnectionRejected(BitStream& bs);
    void HandleRpcWorldVehicleAdd(BitStream& bs);
    void HandleRpcWorldVehicleRemove(BitStream& bs);
    void HandleRpcCreateObject(BitStream& bs);
    void HandleRpcClientCheck(BitStream& bs);

    void HandlePacketPlayerSync(BitStream& bs, uint64_t nowMs);
    void HandlePacketVehicleSync(BitStream& bs, uint64_t nowMs);
    void HandlePacketPassengerSync(BitStream& bs, uint64_t nowMs);
    void HandlePacketAimSync(BitStream& bs, uint64_t nowMs);
    void HandlePacketMarkersSync(BitStream& bs);

    RakNetPeer peer_;
    Events events_;

    std::string host_;
    uint16_t port_ = 7777;
    std::string nickname_;
    std::string password_;
    ProtocolVersion protocol_ = ProtocolVersion::SAMP_037;

    ConnectionState state_ = ConnectionState::DISCONNECTED;
    std::string statusMessage_ = "Disconnected";
    ServerSettings settings_{};
    SpawnInfo spawnInfo_{};
    DialogData activeDialog_{};
    uint16_t currentClassId_ = 0;
    bool inClassSelection_ = false;
    bool autoClassRequested_ = false;
    bool sentFinishedDownloading_ = false;
    int32_t localScore_ = 0;
    int32_t localMoney_ = 0;
    uint32_t localColor_ = 0xFFFFFFFF;

    OnFootSyncData localOnFoot_{};
    InCarSyncData localInCar_{};
    PassengerSyncData localPassenger_{};
    AimSyncData localAim_{};
    bool hasLocalOnFoot_ = false;
    bool hasLocalAim_ = false;
    bool localInVehicle_ = false;
    bool localIsPassenger_ = false;
    uint64_t lastSyncSentMs_ = 0;
    uint64_t lastAimSentMs_ = 0;
    uint64_t lastScoreboardReqMs_ = 0;

    std::map<uint16_t, RemotePlayerState> players_;
    std::map<uint16_t, RemoteVehicleState> vehicles_;
    std::map<uint16_t, WorldObjectState> objects_;
    std::map<int32_t, PickupState> pickups_;
    std::map<uint16_t, TextLabel3D> textLabels_;
    std::map<uint16_t, TextDrawData> textDraws_;
    TextDrawSelectState textDrawSelect_{};
    CheckpointState checkpoint_{};
    AudioStreamState audioStream_{};
};

} // namespace nana::net
