#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace nana::net {

// Protocol profiles supported by Nana SAMP (verified against samp.dll 0.3.DL-R1 & open.mp)
enum class ProtocolVersion : uint32_t {
    SAMP_037 = 4057,   // SA-MP 0.3.7-R1..R5 / open.mp default
    SAMP_03DL = 4062,  // SA-MP 0.3.DL-R1 (0x0FDE)
};

inline const char* ProtocolVersionString(ProtocolVersion v) {
    return (v == ProtocolVersion::SAMP_03DL) ? "0.3.DL-R1" : "0.3.7";
}

// SA-MP 0.3.7 / 0.3.DL-R1 / open.mp RPC Identifiers
enum SampRpcId : uint8_t {
    RPC_SetPlayerName = 11,
    RPC_SetPlayerPos = 12,
    RPC_SetPlayerPosFindZ = 13,
    RPC_SetPlayerHealth = 14,
    RPC_TogglePlayerControllable = 15,
    RPC_PlaySound = 16,
    RPC_SetWorldBounds = 17,
    RPC_GivePlayerMoney = 18,
    RPC_SetPlayerFacingAngle = 19,
    RPC_ResetPlayerMoney = 20,
    RPC_ResetPlayerWeapons = 21,
    RPC_GivePlayerWeapon = 22,
    RPC_ClickPlayer = 23,
    RPC_SetVehicleParams = 24,
    RPC_ClientJoin = 25,
    RPC_EnterVehicle = 26,
    RPC_SetPlayerTime = 29,
    RPC_ToggleClock = 30,
    RPC_WorldPlayerAdd = 32,
    RPC_SetPlayerSkillLevel = 34,
    RPC_SetPlayerDrunkLevel = 35,
    RPC_Create3DTextLabel = 36,
    RPC_DisableCheckpoint = 37,
    RPC_CloseConnection = 40,
    RPC_PlayAudioStream = 41,
    RPC_StopAudioStream = 42,
    RPC_RemoveBuildingForPlayer = 43,
    RPC_CreateObject = 44,
    RPC_SetObjectPos = 45,
    RPC_SetObjectRot = 46,
    RPC_DestroyObject = 47,
    RPC_SetPlayerVirtualWorld = 48,
    RPC_ServerCommand = 50,
    RPC_Spawn = 52,
    RPC_Death = 53,
    RPC_NpcJoin = 54,
    RPC_DeathMessage = 55,
    RPC_SetMapIcon = 56,
    RPC_Delete3DTextLabel = 58,
    RPC_SetPlayerChatBubble = 59,
    RPC_UpdateGameTime = 60,
    RPC_ShowDialog = 61,
    RPC_DialogResponse = 62,
    RPC_DestroyPickup = 63,
    RPC_LinkVehicleToInterior = 65,
    RPC_SetPlayerArmour = 66,
    RPC_SetPlayerArmedWeapon = 67,
    RPC_SetSpawnInfo = 68,
    RPC_SetPlayerTeam = 69,
    RPC_PutPlayerInVehicle = 70,
    RPC_RemovePlayerFromVehicle = 71,
    RPC_SetPlayerColor = 72,
    RPC_DisplayGameText = 73,
    RPC_ForceClassSelection = 74,
    RPC_CreateExplosion = 79,
    RPC_ShowNameTagForPlayer = 80,
    RPC_SelectTextDraw = 83,
    RPC_SetObjectMaterial = 84,
    RPC_ApplyAnimation = 86,
    RPC_ClearAnimations = 87,
    RPC_SetPlayerSpecialAction = 88,
    RPC_SetPlayerFightingStyle = 89,
    RPC_SetPlayerVelocity = 90,
    RPC_SetVehicleVelocity = 91,
    RPC_ClientMessage = 93,
    RPC_SetWorldTime = 94,
    RPC_CreatePickup = 95,
    RPC_ScmEvent = 96,
    RPC_MoveObject = 99,
    RPC_Chat = 101,
    RPC_ClientCheck = 103,
    RPC_EnableStuntBonus = 104,
    RPC_TextDrawSetString = 105,
    RPC_SetCheckpoint = 107,
    RPC_ToggleWidescreen = 111,
    RPC_GiveTakeDamage = 115,
    RPC_InteriorChange = 118,
    RPC_ClickMap = 119,
    RPC_TogglePlayerSpectating = 124,
    RPC_SpectatePlayer = 126,
    RPC_SpectateVehicle = 127,
    RPC_RequestClass = 128,
    RPC_RequestSpawn = 129,
    RPC_ConnectionRejected = 130,
    RPC_SetPlayerWantedLevel = 133,
    RPC_ShowTextDraw = 134,
    RPC_TextDrawHideForPlayer = 135,
    RPC_ServerJoin = 137,
    RPC_ServerQuit = 138,
    RPC_InitGame = 139,
    RPC_SetPlayerAmmo = 145,
    RPC_SetGravity = 146,
    RPC_SetVehicleHealth = 147,
    RPC_SetWeather = 152,
    RPC_SetPlayerSkin = 153,
    RPC_ExitVehicle = 154,
    RPC_UpdateScoresPingsIPs = 155,
    RPC_SetPlayerInterior = 156,
    RPC_SetCameraPos = 157,
    RPC_SetCameraLookAt = 158,
    RPC_SetVehiclePos = 159,
    RPC_SetVehicleZAngle = 160,
    RPC_SetCameraBehindPlayer = 162,
    RPC_WorldPlayerRemove = 163,
    RPC_WorldVehicleAdd = 164,
    RPC_WorldVehicleRemove = 165,
    RPC_WorldPlayerDeath = 166,
    // SA-MP 0.3.DL-R1 custom model download RPCs
    RPC_ModelAdd = 179,
    RPC_RequestDffFile = 181,
    RPC_RequestTxdFile = 182,
    RPC_FinishedDownloading = 185,
};

// SA-MP Dialog Styles
enum SampDialogStyle : uint8_t {
    DIALOG_STYLE_MSGBOX = 0,
    DIALOG_STYLE_INPUT = 1,
    DIALOG_STYLE_LIST = 2,
    DIALOG_STYLE_PASSWORD = 3,
    DIALOG_STYLE_TABLIST = 4,
    DIALOG_STYLE_TABLIST_HEADERS = 5,
};

// GTA SA / SA-MP key bitmasks (OnFoot & InCar)
namespace SampKeys {
    static constexpr uint16_t ACTION        = 1;
    static constexpr uint16_t CROUCH        = 2;
    static constexpr uint16_t FIRE          = 4;
    static constexpr uint16_t SPRINT        = 8;
    static constexpr uint16_t SECONDARY_ATK = 16;
    static constexpr uint16_t JUMP          = 32;
    static constexpr uint16_t LOOK_RIGHT    = 64;
    static constexpr uint16_t HANDBRAKE     = 128;
    static constexpr uint16_t LOOK_LEFT     = 256;
    static constexpr uint16_t SUBMISSION    = 512;
    static constexpr uint16_t WALK          = 1024;
}

static constexpr uint16_t INVALID_PLAYER_ID = 0xFFFF;
static constexpr uint16_t INVALID_VEHICLE_ID = 0xFFFF;
static constexpr uint16_t MAX_PLAYERS = 1004;
static constexpr uint16_t MAX_VEHICLES = 2000;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quat {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    // Matches open.mp GTAQuat convention for Z-axis facing angle (degrees)
    static Quat FromHeadingDegrees(float angleDeg) {
        float halfRad = angleDeg * (3.14159265358979323846f / 180.0f) * -0.5f;
        return Quat{std::cos(halfRad), 0.0f, 0.0f, std::sin(halfRad)};
    }

    // Extracts GTA SA Z-axis facing angle in degrees [0, 360)
    float ToHeadingDegrees() const {
        float siny = 2.0f * (x * y + z * w);
        float cosy = 1.0f - 2.0f * (x * x + z * z);
        float deg = -std::atan2(siny, cosy) * (180.0f / 3.14159265358979323846f);
        while (deg < 0.0f) deg += 360.0f;
        while (deg >= 360.0f) deg -= 360.0f;
        return deg;
    }
};

struct OnFootSyncData {
    uint16_t leftRight = 0;
    uint16_t upDown = 0;
    uint16_t keys = 0;
    Vec3 position{};
    Quat quaternion{};
    uint8_t health = 100;
    uint8_t armour = 0;
    uint8_t weapon = 0;
    uint8_t specialAction = 0;
    Vec3 velocity{};
    Vec3 surfingOffset{};
    uint16_t surfingVehicleId = 0;
    uint16_t animationId = 0;
    uint16_t animationFlags = 0;
};

struct InCarSyncData {
    uint16_t vehicleId = 0;
    uint16_t leftRight = 0;
    uint16_t upDown = 0;
    uint16_t keys = 0;
    Quat quaternion{};
    Vec3 position{};
    Vec3 velocity{};
    float carHealth = 1000.0f;
    uint8_t playerHealth = 100;
    uint8_t playerArmour = 0;
    uint8_t weapon = 0;
    uint8_t sirenOn = 0;
    uint8_t landingGear = 0;
    uint16_t trailerId = 0;
    float trainSpeed = 0.0f;
};

struct PassengerSyncData {
    uint16_t vehicleId = 0;
    uint8_t seatId = 1;
    bool driveBy = false;
    bool cuffed = false;
    uint8_t weapon = 0;
    uint8_t health = 100;
    uint8_t armour = 0;
    uint16_t leftRight = 0;
    uint16_t upDown = 0;
    uint16_t keys = 0;
    Vec3 position{};
};

struct AimSyncData {
    uint8_t camMode = 4;
    Vec3 camFront{0.0f, 1.0f, 0.0f};
    Vec3 camPos{};
    float aimZ = 0.0f;
    uint8_t camExtZoom = 0;
    uint8_t weaponState = 0;
    uint8_t aspectRatio = 0x55;
};

struct DialogData {
    int16_t dialogId = -1;
    uint8_t style = DIALOG_STYLE_MSGBOX;
    std::string title;
    std::string button1;
    std::string button2;
    std::string info;
};

struct ScoreboardEntry {
    uint16_t playerId = 0;
    std::string nickname;
    int32_t score = 0;
    uint32_t ping = 0;
    uint32_t colorRgba = 0xFFFFFFFF;
    bool isLocal = false;
};

struct WorldObjectState {
    uint16_t objectId = 0;
    int32_t modelId = 0;
    Vec3 position{};
    Vec3 rotation{};
    float drawDistance = 300.0f;
};

struct RemovedBuildingRule {
    uint32_t modelId = 0;
    Vec3 center{};
    float radius = 0.0f;
};

struct PickupState {
    int32_t pickupId = 0;
    int32_t modelId = 0;
    int32_t type = 0;
    Vec3 position{};
};

struct CheckpointState {
    bool active = false;
    Vec3 position{};
    float radius = 3.0f;
};

struct TextLabel3D {
    uint16_t id = 0;
    uint32_t colorRgba = 0xFFFFFFFF;
    Vec3 position{};
    float drawDistance = 30.0f;
    bool testLos = false;
    uint16_t attachedPlayerId = INVALID_PLAYER_ID;
    uint16_t attachedVehicleId = INVALID_VEHICLE_ID;
    std::string text;
};

struct AudioStreamState {
    bool playing = false;
    std::string url;
    Vec3 position{};
    float radius = 0.0f;
    bool usePos = false;
};

struct TextDrawData {
    uint16_t id = 0;            // 0..2047 global, 2048..2303 player textdraw
    bool useBox = false;
    uint8_t alignment = 1;      // 1 = left, 2 = center, 3 = right
    bool proportional = true;
    float letterWidth = 0.48f;
    float letterHeight = 1.12f;
    uint32_t letterColorAbgr = 0xFFE1E1E1u;
    float lineWidth = 640.0f;   // textSize.x (in 640x480 virtual coords)
    float lineHeight = 0.0f;    // textSize.y
    uint32_t boxColorAbgr = 0x80808080u;
    uint8_t shadow = 2;
    uint8_t outline = 0;
    uint32_t bgColorAbgr = 0xFF000000u;
    uint8_t style = 1;          // 0..3 fonts, 4 = TXD sprite, 5 = preview model
    bool selectable = false;
    float x = 0.0f;             // 0..640 virtual X
    float y = 0.0f;             // 0..480 virtual Y
    uint16_t modelId = 0;
    float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f;
    float zoom = 1.0f;
    int16_t color1 = -1;
    int16_t color2 = -1;
    std::string text;
};

struct TextDrawSelectState {
    bool active = false;
    uint32_t hoverColorRgba = 0xFFFF00FFu;
};

struct ServerSettings {
    bool zoneNames = false;
    bool useCJWalk = false;
    bool allowWeapons = true;
    bool limitGlobalChatRadius = false;
    float globalChatRadius = 200.0f;
    bool stuntBonus = true;
    float nametagDrawDist = 70.0f;
    bool disableEnterExits = false;
    bool nametagLOS = true;
    bool manualVehEngineAndLight = false;
    uint32_t spawnsAvailable = 0;
    uint16_t localPlayerId = INVALID_PLAYER_ID;
    bool showPlayerTags = true;
    uint32_t showPlayerMarkers = 1;
    uint8_t worldTime = 12;
    uint8_t weather = 10;
    float gravity = 0.008f;
    bool lanMode = false;
    uint32_t deathDropMoney = 0;
    bool instagib = false;
    uint32_t onFootRate = 40;
    uint32_t inCarRate = 40;
    uint32_t weaponRate = 40;
    uint32_t multiplier = 10;
    uint32_t lagCompMode = 1;
    std::string serverName;
    std::array<uint8_t, 212> vehicleModels{};
    uint32_t vehicleFriendlyFire = 0;
};

struct SpawnInfo {
    uint8_t team = 255;
    uint32_t skin = 0;
    Vec3 position{};
    float rotation = 0.0f;
    std::array<uint32_t, 3> weapons{};
    std::array<uint32_t, 3> ammo{};
};

struct RemotePlayerState {
    uint16_t playerId = INVALID_PLAYER_ID;
    std::string name;
    uint32_t color = 0xFFFFFFFF;
    uint8_t isNpc = 0;
    int32_t score = 0;
    uint32_t ping = 0;
    bool streamedIn = false;
    uint8_t team = 255;
    uint32_t skin = 0;
    bool inVehicle = false;
    bool isPassenger = false;
    OnFootSyncData onFoot{};
    InCarSyncData inCar{};
    PassengerSyncData passenger{};
    AimSyncData aim{};
    uint64_t lastSyncTimeMs = 0;
};

struct RemoteVehicleState {
    uint16_t vehicleId = INVALID_VEHICLE_ID;
    uint32_t modelId = 400;
    Vec3 position{};
    float angle = 0.0f;
    Quat quaternion{};
    Vec3 velocity{};
    uint8_t color1 = 0;
    uint8_t color2 = 0;
    float health = 1000.0f;
    uint8_t interior = 0;
    bool streamedIn = false;
};

enum class ConnectionState : int {
    DISCONNECTED = 0,
    CONNECTING = 1,
    JOINING = 2,
    CONNECTED = 3,
    SPAWNED = 4,
    FAILED = 5,
};

} // namespace nana::net
