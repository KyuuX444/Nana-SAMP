#include "samp_client.h"
#include "samp_crypto.h"
#include "string_compressor.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace nana::net {

SampClient::SampClient() {
    RakNetPeer::Callbacks cb;
    cb.onConnected = [this](uint16_t idx, uint32_t challenge) {
        OnPeerConnected(idx, challenge);
    };
    cb.onDisconnected = [this](uint8_t reasonId, const std::string& text) {
        OnPeerDisconnected(reasonId, text);
    };
    cb.onRpcReceived = [this](uint8_t rpcId, BitStream& bs) {
        OnRpcReceived(rpcId, bs);
    };
    cb.onPacketReceived = [this](uint8_t packetId, BitStream& bs) {
        OnPacketReceived(packetId, bs);
    };
    peer_.SetCallbacks(std::move(cb));
}

SampClient::~SampClient() {
    Disconnect();
}

uint64_t SampClient::NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

uint8_t SampClient::UnpackHealthArmourNibble(uint8_t nibble) {
    nibble &= 0x0F;
    if (nibble == 0x0F) return 100;
    if (nibble == 0) return 0;
    return static_cast<uint8_t>(nibble * 7);
}

void SampClient::SetState(ConnectionState st, const std::string& message) {
    state_ = st;
    statusMessage_ = message;
    if (events_.onStateChanged) {
        events_.onStateChanged(st, message);
    }
}

void SampClient::EmitChat(uint32_t rgba, const std::string& text) {
    if (events_.onChatMessage) {
        events_.onChatMessage(rgba, text);
    }
}

bool SampClient::Connect(const std::string& host, uint16_t port,
                         const std::string& nickname, const std::string& password,
                         ProtocolVersion protocol) {
    Disconnect();

    host_ = host;
    port_ = port;
    nickname_ = nickname.substr(0, 24);
    password_ = password;
    protocol_ = protocol;
    settings_ = ServerSettings{};
    spawnInfo_ = SpawnInfo{};
    activeDialog_ = DialogData{};
    currentClassId_ = 0;
    inClassSelection_ = false;
    autoClassRequested_ = false;
    sentFinishedDownloading_ = false;
    localScore_ = 0;
    localMoney_ = 0;
    localColor_ = 0xFFFFFFFF;
    hasLocalOnFoot_ = false;
    hasLocalAim_ = false;
    localInVehicle_ = false;
    localIsPassenger_ = false;
    lastSyncSentMs_ = 0;
    lastAimSentMs_ = 0;
    lastScoreboardReqMs_ = 0;
    players_.clear();
    vehicles_.clear();
    objects_.clear();
    pickups_.clear();
    textLabels_.clear();
    textDraws_.clear();
    textDrawSelect_ = TextDrawSelectState{};
    checkpoint_ = CheckpointState{};
    audioStream_ = AudioStreamState{};

    if (nickname_.size() < 3) {
        SetState(ConnectionState::FAILED, "Nickname must be at least 3 characters");
        return false;
    }

    SetState(ConnectionState::CONNECTING,
             "Connecting (" + std::string(ProtocolVersionString(protocol_)) + ") to " +
             host_ + ":" + std::to_string(port_) + "...");
    EmitChat(0xA9C4E4FF, "Connecting to " + host_ + ":" + std::to_string(port_) +
             " [" + ProtocolVersionString(protocol_) + "]...");

    if (!peer_.Connect(host_, port_, password_)) {
        SetState(ConnectionState::FAILED, "Failed to resolve or open socket for " + host_);
        EmitChat(0xFF6666FF, "Connection failed: could not resolve server address.");
        return false;
    }
    return true;
}

void SampClient::Disconnect() {
    if (state_ != ConnectionState::DISCONNECTED) {
        peer_.Disconnect(true);
        players_.clear();
        vehicles_.clear();
        objects_.clear();
        pickups_.clear();
        textLabels_.clear();
        textDraws_.clear();
        textDrawSelect_ = TextDrawSelectState{};
        inClassSelection_ = false;
        activeDialog_ = DialogData{};
        if (audioStream_.playing) {
            audioStream_.playing = false;
            if (events_.onAudioStream) events_.onAudioStream(audioStream_);
        }
        SetState(ConnectionState::DISCONNECTED, "Disconnected");
    }
}

bool SampClient::Reconnect() {
    if (host_.empty() || port_ == 0 || nickname_.empty()) return false;
    return Connect(host_, port_, nickname_, password_, protocol_);
}

void SampClient::Tick() {
    if (state_ == ConnectionState::DISCONNECTED || state_ == ConnectionState::FAILED) {
        return;
    }
    peer_.Tick();

    uint64_t now = NowMs();
    if (state_ >= ConnectionState::CONNECTED) {
        // Refresh scoreboard scores & pings periodically (every 3 seconds)
        if (lastScoreboardReqMs_ == 0 || now - lastScoreboardReqMs_ >= 3000) {
            RequestScoreboardUpdate();
            lastScoreboardReqMs_ = now;
        }
    }
    if (state_ == ConnectionState::SPAWNED) {
        SendSyncPacketsIfDue(now);
    }
}

void SampClient::OnPeerConnected(uint16_t playerIndex, uint32_t serverChallenge) {
    settings_.localPlayerId = playerIndex;
    SetState(ConnectionState::JOINING, "Connected. Joining game as " + nickname_ + "...");
    EmitChat(0xA9C4E4FF, "Connected. Joining the game...");
    SendClientJoin(serverChallenge);
}

void SampClient::OnPeerDisconnected(uint8_t /*reasonPacketId*/, const std::string& reasonText) {
    players_.clear();
    vehicles_.clear();
    objects_.clear();
    inClassSelection_ = false;
    if (audioStream_.playing) {
        audioStream_.playing = false;
        if (events_.onAudioStream) events_.onAudioStream(audioStream_);
    }
    SetState(ConnectionState::FAILED, reasonText);
    EmitChat(0xFF6666FF, reasonText);
}

void SampClient::SendClientJoin(uint32_t serverChallenge) {
    BitStream bs;
    uint32_t version = static_cast<uint32_t>(protocol_);
    uint8_t mod = 1;
    uint32_t challengeResponse = serverChallenge ^ version;
    std::string serial = SampCrypto::GenerateClientSerial(nickname_ + "@" + host_);
    std::string versionStr = ProtocolVersionString(protocol_);

    bs.Write<uint32_t>(version);
    bs.Write<uint8_t>(mod);
    bs.WriteDynStr8(nickname_);
    bs.Write<uint32_t>(challengeResponse);
    bs.WriteDynStr8(serial);
    bs.WriteDynStr8(versionStr);

    peer_.SendRpc(RPC_ClientJoin, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::SendFinishedDownloading() {
    if (!peer_.IsConnected() || sentFinishedDownloading_) return;
    sentFinishedDownloading_ = true;
    BitStream bs;
    peer_.SendRpc(RPC_FinishedDownloading, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::RequestClass(uint16_t classId) {
    if (!peer_.IsConnected()) return;
    currentClassId_ = classId;
    BitStream bs;
    bs.Write<uint16_t>(classId);
    peer_.SendRpc(RPC_RequestClass, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::RequestNextClass(int delta) {
    uint32_t count = settings_.spawnsAvailable;
    if (count == 0) count = 1;
    int next = (static_cast<int>(currentClassId_) + delta) % static_cast<int>(count);
    if (next < 0) next += static_cast<int>(count);
    RequestClass(static_cast<uint16_t>(next));
}

void SampClient::RequestSpawn() {
    if (!peer_.IsConnected()) return;
    BitStream bs;
    peer_.SendRpc(RPC_RequestSpawn, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::SendChat(const std::string& text) {
    if (!peer_.IsConnected() || text.empty()) return;
    if (text[0] == '/') {
        BitStream bs;
        bs.WriteDynStr32(text);
        peer_.SendRpc(RPC_ServerCommand, bs, PacketReliability::RELIABLE_ORDERED, 2);
    } else {
        BitStream bs;
        bs.WriteDynStr8(text.substr(0, 144));
        peer_.SendRpc(RPC_Chat, bs, PacketReliability::RELIABLE_ORDERED, 2);
    }
}

void SampClient::SendDialogResponse(int16_t dialogId, uint8_t button,
                                    int16_t listItem, const std::string& inputText) {
    if (!peer_.IsConnected()) return;
    BitStream bs;
    bs.Write<int16_t>(dialogId);
    bs.Write<uint8_t>(button);
    bs.Write<int16_t>(listItem);
    bs.WriteDynStr8(inputText.substr(0, 128));
    peer_.SendRpc(RPC_DialogResponse, bs, PacketReliability::RELIABLE_ORDERED, 2);
    if (activeDialog_.dialogId == dialogId) {
        activeDialog_.dialogId = -1;
        if (events_.onHideDialog) events_.onHideDialog();
    }
}

void SampClient::RequestScoreboardUpdate() {
    if (!peer_.IsConnected()) return;
    BitStream bs;
    peer_.SendRpc(RPC_UpdateScoresPingsIPs, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::SendClickPlayer(uint16_t playerId, uint8_t source) {
    if (!peer_.IsConnected()) return;
    BitStream bs;
    bs.Write<uint16_t>(playerId);
    bs.Write<uint8_t>(source);
    peer_.SendRpc(RPC_ClickPlayer, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::SendDeath(uint8_t reason, uint16_t killerId) {
    if (!peer_.IsConnected()) return;
    BitStream bs;
    bs.Write<uint8_t>(reason);
    bs.Write<uint16_t>(killerId);
    peer_.SendRpc(RPC_Death, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::SendEnterVehicle(uint16_t vehicleId, bool passenger) {
    if (!peer_.IsConnected()) return;
    BitStream bs;
    bs.Write<uint16_t>(vehicleId);
    bs.Write<uint8_t>(passenger ? 1 : 0);
    peer_.SendRpc(RPC_EnterVehicle, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::SendExitVehicle(uint16_t vehicleId) {
    if (!peer_.IsConnected()) return;
    BitStream bs;
    bs.Write<uint16_t>(vehicleId);
    peer_.SendRpc(RPC_ExitVehicle, bs, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::SendClickTextDraw(uint16_t textDrawId) {
    if (!peer_.IsConnected()) return;
    BitStream bs;
    bs.Write<uint16_t>(textDrawId);
    peer_.SendRpc(RPC_SelectTextDraw, bs, PacketReliability::RELIABLE_ORDERED, 2);
    if (textDrawId == 0xFFFFu) {
        textDrawSelect_.active = false;
        if (events_.onTextDrawSelectChanged) events_.onTextDrawSelectChanged(textDrawSelect_);
    }
}

void SampClient::UpdateLocalOnFoot(const OnFootSyncData& data) {
    localOnFoot_ = data;
    hasLocalOnFoot_ = true;
}

void SampClient::UpdateLocalInCar(const InCarSyncData& data) {
    localInCar_ = data;
    localOnFoot_.position = data.position;
    hasLocalOnFoot_ = true;
}

void SampClient::UpdateLocalPassenger(const PassengerSyncData& data) {
    localPassenger_ = data;
    localOnFoot_.position = data.position;
    hasLocalOnFoot_ = true;
}

void SampClient::UpdateLocalAim(const AimSyncData& data) {
    localAim_ = data;
    hasLocalAim_ = true;
}

std::vector<ScoreboardEntry> SampClient::GetScoreboardEntries() const {
    std::vector<ScoreboardEntry> list;
    list.reserve(players_.size() + 1);
    if (state_ >= ConnectionState::CONNECTED) {
        ScoreboardEntry self{};
        self.playerId = settings_.localPlayerId;
        self.nickname = nickname_;
        self.score = localScore_;
        self.ping = peer_.GetPingMs();
        self.colorRgba = localColor_;
        self.isLocal = true;
        list.push_back(std::move(self));
    }
    for (const auto& [pid, p] : players_) {
        ScoreboardEntry e{};
        e.playerId = pid;
        e.nickname = p.name;
        e.score = p.score;
        e.ping = p.ping;
        e.colorRgba = p.color;
        e.isLocal = false;
        list.push_back(std::move(e));
    }
    std::sort(list.begin(), list.end(), [](const ScoreboardEntry& a, const ScoreboardEntry& b) {
        return a.playerId < b.playerId;
    });
    return list;
}

void SampClient::SendSyncPacketsIfDue(uint64_t nowMs) {
    if (!peer_.IsConnected() || !hasLocalOnFoot_) return;

    uint32_t rateMs = localInVehicle_ ? settings_.inCarRate : settings_.onFootRate;
    if (rateMs < 20) rateMs = 40;
    if (rateMs > 250) rateMs = 100;

    if (lastSyncSentMs_ == 0 || (nowMs - lastSyncSentMs_) >= rateMs) {
        BitStream bs;
        if (localInVehicle_ && localIsPassenger_ && localPassenger_.vehicleId != 0) {
            WriteClientPassengerSync(bs, localPassenger_);
        } else if (localInVehicle_ && localInCar_.vehicleId != 0) {
            WriteClientInCarSync(bs, localInCar_);
        } else {
            WriteClientOnFootSync(bs, localOnFoot_);
        }
        peer_.SendPacket(bs, PacketReliability::UNRELIABLE_SEQUENCED, 0);
        lastSyncSentMs_ = nowMs;
    }

    if (hasLocalAim_ && (lastAimSentMs_ == 0 || (nowMs - lastAimSentMs_) >= 150)) {
        BitStream aimBs;
        WriteClientAimSync(aimBs, localAim_);
        peer_.SendPacket(aimBs, PacketReliability::UNRELIABLE_SEQUENCED, 0);
        lastAimSentMs_ = nowMs;
    }
}

void SampClient::WriteClientOnFootSync(BitStream& bs, const OnFootSyncData& d) {
    bs.Write<uint8_t>(ID_PLAYER_SYNC);
    bs.Write<uint16_t>(d.leftRight);
    bs.Write<uint16_t>(d.upDown);
    bs.Write<uint16_t>(d.keys);
    bs.Write<float>(d.position.x);
    bs.Write<float>(d.position.y);
    bs.Write<float>(d.position.z);
    bs.Write<float>(d.quaternion.w);
    bs.Write<float>(d.quaternion.x);
    bs.Write<float>(d.quaternion.y);
    bs.Write<float>(d.quaternion.z);
    bs.Write<uint8_t>(d.health);
    bs.Write<uint8_t>(d.armour);
    bs.Write<uint8_t>(d.weapon);
    bs.Write<uint8_t>(d.specialAction);
    bs.Write<float>(d.velocity.x);
    bs.Write<float>(d.velocity.y);
    bs.Write<float>(d.velocity.z);
    bs.Write<float>(d.surfingOffset.x);
    bs.Write<float>(d.surfingOffset.y);
    bs.Write<float>(d.surfingOffset.z);
    bs.Write<uint16_t>(d.surfingVehicleId);
    bs.Write<uint16_t>(d.animationId);
    bs.Write<uint16_t>(d.animationFlags);
}

void SampClient::WriteClientInCarSync(BitStream& bs, const InCarSyncData& d) {
    bs.Write<uint8_t>(ID_VEHICLE_SYNC);
    bs.Write<uint16_t>(d.vehicleId);
    bs.Write<uint16_t>(d.leftRight);
    bs.Write<uint16_t>(d.upDown);
    bs.Write<uint16_t>(d.keys);
    bs.Write<float>(d.quaternion.w);
    bs.Write<float>(d.quaternion.x);
    bs.Write<float>(d.quaternion.y);
    bs.Write<float>(d.quaternion.z);
    bs.Write<float>(d.position.x);
    bs.Write<float>(d.position.y);
    bs.Write<float>(d.position.z);
    bs.Write<float>(d.velocity.x);
    bs.Write<float>(d.velocity.y);
    bs.Write<float>(d.velocity.z);
    bs.Write<float>(d.carHealth);
    bs.Write<uint8_t>(d.playerHealth);
    bs.Write<uint8_t>(d.playerArmour);
    bs.Write<uint8_t>(d.weapon);
    bs.Write<uint8_t>(d.sirenOn);
    bs.Write<uint8_t>(d.landingGear);
    bs.Write<uint16_t>(d.trailerId);
    bs.Write<float>(d.trainSpeed);
}

void SampClient::WriteClientPassengerSync(BitStream& bs, const PassengerSyncData& d) {
    bs.Write<uint8_t>(ID_PASSENGER_SYNC);
    bs.Write<uint16_t>(d.vehicleId);
    uint8_t seatFlags = static_cast<uint8_t>((d.seatId & 0x3F) |
                        (d.driveBy ? 0x40 : 0) | (d.cuffed ? 0x80 : 0));
    bs.Write<uint8_t>(seatFlags);
    bs.Write<uint8_t>(d.weapon);
    bs.Write<uint8_t>(d.health);
    bs.Write<uint8_t>(d.armour);
    bs.Write<uint16_t>(d.leftRight);
    bs.Write<uint16_t>(d.upDown);
    bs.Write<uint16_t>(d.keys);
    bs.Write<float>(d.position.x);
    bs.Write<float>(d.position.y);
    bs.Write<float>(d.position.z);
}

void SampClient::WriteClientAimSync(BitStream& bs, const AimSyncData& d) {
    bs.Write<uint8_t>(ID_AIM_SYNC);
    bs.Write<uint8_t>(d.camMode);
    bs.Write<float>(d.camFront.x);
    bs.Write<float>(d.camFront.y);
    bs.Write<float>(d.camFront.z);
    bs.Write<float>(d.camPos.x);
    bs.Write<float>(d.camPos.y);
    bs.Write<float>(d.camPos.z);
    bs.Write<float>(d.aimZ);
    uint8_t zoomWep = static_cast<uint8_t>((d.camExtZoom & 0x3F) | ((d.weaponState & 0x03) << 6));
    bs.Write<uint8_t>(zoomWep);
    bs.Write<uint8_t>(d.aspectRatio);
}

bool SampClient::ReadServerOnFootSync(BitStream& bs, uint16_t& outPlayerId, OnFootSyncData& out) {
    uint8_t packetId = 0;
    if (!bs.Read<uint8_t>(packetId) || packetId != ID_PLAYER_SYNC) return false;
    if (!bs.Read<uint16_t>(outPlayerId)) return false;

    out = OnFootSyncData{};
    bool hasLR = false;
    if (!bs.Read<bool>(hasLR)) return false;
    if (hasLR && !bs.Read<uint16_t>(out.leftRight)) return false;

    bool hasUD = false;
    if (!bs.Read<bool>(hasUD)) return false;
    if (hasUD && !bs.Read<uint16_t>(out.upDown)) return false;

    if (!bs.Read<uint16_t>(out.keys)) return false;
    if (!bs.Read<float>(out.position.x) || !bs.Read<float>(out.position.y) || !bs.Read<float>(out.position.z)) {
        return false;
    }
    if (!bs.ReadNormQuat(out.quaternion.w, out.quaternion.x, out.quaternion.y, out.quaternion.z)) {
        return false;
    }

    uint8_t packedHA = 0;
    if (!bs.Read<uint8_t>(packedHA)) return false;
    out.health = UnpackHealthArmourNibble(packedHA >> 4);
    out.armour = UnpackHealthArmourNibble(packedHA & 0x0F);

    if (!bs.Read<uint8_t>(out.weapon)) return false;
    if (!bs.Read<uint8_t>(out.specialAction)) return false;
    if (!bs.ReadVector(out.velocity.x, out.velocity.y, out.velocity.z)) return false;

    bool hasSurf = false;
    if (bs.Read<bool>(hasSurf) && hasSurf) {
        bs.Read<uint16_t>(out.surfingVehicleId);
        bs.Read<float>(out.surfingOffset.x);
        bs.Read<float>(out.surfingOffset.y);
        bs.Read<float>(out.surfingOffset.z);
    }

    bool hasAnim = false;
    if (bs.Read<bool>(hasAnim) && hasAnim) {
        bs.Read<uint16_t>(out.animationId);
        bs.Read<uint16_t>(out.animationFlags);
    }
    return true;
}

bool SampClient::ReadServerInCarSync(BitStream& bs, uint16_t& outPlayerId, InCarSyncData& out) {
    uint8_t packetId = 0;
    if (!bs.Read<uint8_t>(packetId) || packetId != ID_VEHICLE_SYNC) return false;
    if (!bs.Read<uint16_t>(outPlayerId)) return false;

    out = InCarSyncData{};
    if (!bs.Read<uint16_t>(out.vehicleId)) return false;
    if (!bs.Read<uint16_t>(out.leftRight) || !bs.Read<uint16_t>(out.upDown) || !bs.Read<uint16_t>(out.keys)) {
        return false;
    }
    if (!bs.ReadNormQuat(out.quaternion.w, out.quaternion.x, out.quaternion.y, out.quaternion.z)) {
        return false;
    }
    if (!bs.Read<float>(out.position.x) || !bs.Read<float>(out.position.y) || !bs.Read<float>(out.position.z)) {
        return false;
    }
    if (!bs.ReadVector(out.velocity.x, out.velocity.y, out.velocity.z)) return false;

    uint16_t carHp = 1000;
    if (!bs.Read<uint16_t>(carHp)) return false;
    out.carHealth = static_cast<float>(carHp);

    uint8_t packedHA = 0;
    if (!bs.Read<uint8_t>(packedHA)) return false;
    out.playerHealth = UnpackHealthArmourNibble(packedHA >> 4);
    out.playerArmour = UnpackHealthArmourNibble(packedHA & 0x0F);

    bs.Read<uint8_t>(out.weapon);
    bool siren = false;
    if (bs.Read<bool>(siren)) out.sirenOn = siren ? 1 : 0;
    bool gear = false;
    if (bs.Read<bool>(gear)) out.landingGear = gear ? 1 : 0;
    bool hasTrain = false;
    if (bs.Read<bool>(hasTrain) && hasTrain) {
        bs.Read<float>(out.trainSpeed);
    }
    bool hasTrailer = false;
    if (bs.Read<bool>(hasTrailer) && hasTrailer) {
        bs.Read<uint16_t>(out.trailerId);
    }
    return true;
}

bool SampClient::ReadServerPassengerSync(BitStream& bs, uint16_t& outPlayerId, PassengerSyncData& out) {
    uint8_t packetId = 0;
    if (!bs.Read<uint8_t>(packetId) || packetId != ID_PASSENGER_SYNC) return false;
    if (!bs.Read<uint16_t>(outPlayerId)) return false;

    out = PassengerSyncData{};
    if (!bs.Read<uint16_t>(out.vehicleId)) return false;
    uint8_t seatFlags = 0;
    if (!bs.Read<uint8_t>(seatFlags)) return false;
    out.seatId = seatFlags & 0x3F;
    out.driveBy = (seatFlags & 0x40) != 0;
    out.cuffed = (seatFlags & 0x80) != 0;
    if (!bs.Read<uint8_t>(out.weapon) || !bs.Read<uint8_t>(out.health) || !bs.Read<uint8_t>(out.armour)) {
        return false;
    }
    if (!bs.Read<uint16_t>(out.leftRight) || !bs.Read<uint16_t>(out.upDown) || !bs.Read<uint16_t>(out.keys)) {
        return false;
    }
    return bs.Read<float>(out.position.x) && bs.Read<float>(out.position.y) && bs.Read<float>(out.position.z);
}

bool SampClient::ReadServerShowDialog(BitStream& bs, DialogData& out) {
    out = DialogData{};
    if (!bs.Read<int16_t>(out.dialogId)) return false;
    if (!bs.Read<uint8_t>(out.style)) return false;
    if (!bs.ReadDynStr8(out.title)) return false;
    if (!bs.ReadDynStr8(out.button1)) return false;
    if (!bs.ReadDynStr8(out.button2)) return false;
    if (bs.GetNumberOfUnreadBits() > 0) {
        StringCompressor::Instance().DecodeString(out.info, 4096, bs);
    }
    return true;
}

bool SampClient::ReadServerShowTextDraw(BitStream& bs, TextDrawData& out) {
    out = TextDrawData{};
    uint8_t flags = 0;
    uint8_t selectable = 0;
    if (!bs.Read<uint16_t>(out.id) ||
        !bs.Read<uint8_t>(flags) ||
        !bs.Read<float>(out.letterWidth) ||
        !bs.Read<float>(out.letterHeight) ||
        !bs.Read<uint32_t>(out.letterColorAbgr) ||
        !bs.Read<float>(out.lineWidth) ||
        !bs.Read<float>(out.lineHeight) ||
        !bs.Read<uint32_t>(out.boxColorAbgr) ||
        !bs.Read<uint8_t>(out.shadow) ||
        !bs.Read<uint8_t>(out.outline) ||
        !bs.Read<uint32_t>(out.bgColorAbgr) ||
        !bs.Read<uint8_t>(out.style) ||
        !bs.Read<uint8_t>(selectable) ||
        !bs.Read<float>(out.x) ||
        !bs.Read<float>(out.y) ||
        !bs.Read<uint16_t>(out.modelId) ||
        !bs.Read<float>(out.rotX) ||
        !bs.Read<float>(out.rotY) ||
        !bs.Read<float>(out.rotZ) ||
        !bs.Read<float>(out.zoom) ||
        !bs.Read<int16_t>(out.color1) ||
        !bs.Read<int16_t>(out.color2) ||
        !bs.ReadDynStr16(out.text)) {
        return false;
    }
    out.useBox = (flags & 0x01u) != 0;
    if (flags & 0x08u) out.alignment = 2;       // Center (bit 3)
    else if (flags & 0x04u) out.alignment = 3;  // Right (bit 2)
    else out.alignment = 1;                     // Left (default / bit 1)
    out.proportional = (flags & 0x10u) != 0;
    out.selectable = (selectable != 0);
    return true;
}

void SampClient::OnRpcReceived(uint8_t rpcId, BitStream& bs) {
    switch (rpcId) {
        case RPC_InitGame:
            HandleRpcInitGame(bs);
            break;
        case RPC_ServerJoin:
            HandleRpcServerJoin(bs);
            break;
        case RPC_ServerQuit:
            HandleRpcServerQuit(bs);
            break;
        case RPC_ClientMessage:
            HandleRpcClientMessage(bs);
            break;
        case RPC_Chat:
            HandleRpcChat(bs);
            break;
        case RPC_ShowDialog:
            HandleRpcShowDialog(bs);
            break;
        case RPC_UpdateScoresPingsIPs:
            HandleRpcUpdateScoresPings(bs);
            break;
        case RPC_RequestClass:
            HandleRpcRequestClass(bs);
            break;
        case RPC_RequestSpawn:
            HandleRpcRequestSpawn(bs);
            break;
        case RPC_SetSpawnInfo:
            HandleRpcSetSpawnInfo(bs);
            break;
        case RPC_WorldPlayerAdd:
            HandleRpcWorldPlayerAdd(bs);
            break;
        case RPC_WorldPlayerRemove:
            HandleRpcWorldPlayerRemove(bs);
            break;
        case RPC_ConnectionRejected:
            HandleRpcConnectionRejected(bs);
            break;
        case RPC_WorldVehicleAdd:
            HandleRpcWorldVehicleAdd(bs);
            break;
        case RPC_WorldVehicleRemove:
            HandleRpcWorldVehicleRemove(bs);
            break;
        case RPC_CreateObject:
            HandleRpcCreateObject(bs);
            break;
        case RPC_SetObjectPos: {
            uint16_t oid = 0;
            Vec3 pos{};
            if (bs.Read<uint16_t>(oid) && bs.Read<float>(pos.x) && bs.Read<float>(pos.y) && bs.Read<float>(pos.z)) {
                auto it = objects_.find(oid);
                if (it != objects_.end()) {
                    it->second.position = pos;
                    if (events_.onObjectUpdated) events_.onObjectUpdated(it->second);
                }
            }
            break;
        }
        case RPC_SetObjectRot: {
            uint16_t oid = 0;
            Vec3 rot{};
            if (bs.Read<uint16_t>(oid) && bs.Read<float>(rot.x) && bs.Read<float>(rot.y) && bs.Read<float>(rot.z)) {
                auto it = objects_.find(oid);
                if (it != objects_.end()) {
                    it->second.rotation = rot;
                    if (events_.onObjectUpdated) events_.onObjectUpdated(it->second);
                }
            }
            break;
        }
        case RPC_MoveObject: {
            uint16_t oid = 0;
            Vec3 from{}, to{};
            float speed = 0.0f;
            if (bs.Read<uint16_t>(oid) &&
                bs.Read<float>(from.x) && bs.Read<float>(from.y) && bs.Read<float>(from.z) &&
                bs.Read<float>(to.x) && bs.Read<float>(to.y) && bs.Read<float>(to.z) &&
                bs.Read<float>(speed)) {
                auto it = objects_.find(oid);
                if (it != objects_.end()) {
                    it->second.position = to;
                    Vec3 rot{};
                    if (bs.Read<float>(rot.x) && bs.Read<float>(rot.y) && bs.Read<float>(rot.z)) {
                        it->second.rotation = rot;
                    }
                    if (events_.onObjectUpdated) events_.onObjectUpdated(it->second);
                }
            }
            break;
        }
        case RPC_DestroyObject: {
            uint16_t oid = 0;
            if (bs.Read<uint16_t>(oid)) {
                objects_.erase(oid);
                if (events_.onObjectDestroyed) events_.onObjectDestroyed(oid);
            }
            break;
        }
        case RPC_RemoveBuildingForPlayer: {
            RemovedBuildingRule rule{};
            if (bs.Read<uint32_t>(rule.modelId) &&
                bs.Read<float>(rule.center.x) && bs.Read<float>(rule.center.y) && bs.Read<float>(rule.center.z) &&
                bs.Read<float>(rule.radius)) {
                if (events_.onRemoveBuilding) events_.onRemoveBuilding(rule);
            }
            break;
        }
        case RPC_CreatePickup: {
            PickupState pk{};
            if (bs.Read<int32_t>(pk.pickupId) && bs.Read<int32_t>(pk.modelId) && bs.Read<int32_t>(pk.type) &&
                bs.Read<float>(pk.position.x) && bs.Read<float>(pk.position.y) && bs.Read<float>(pk.position.z)) {
                pickups_[pk.pickupId] = pk;
                if (events_.onPickupCreated) events_.onPickupCreated(pk);
            }
            break;
        }
        case RPC_DestroyPickup: {
            int32_t pid = 0;
            if (bs.Read<int32_t>(pid)) {
                pickups_.erase(pid);
                if (events_.onPickupDestroyed) events_.onPickupDestroyed(pid);
            }
            break;
        }
        case RPC_SetCheckpoint: {
            if (bs.Read<float>(checkpoint_.position.x) && bs.Read<float>(checkpoint_.position.y) &&
                bs.Read<float>(checkpoint_.position.z) && bs.Read<float>(checkpoint_.radius)) {
                checkpoint_.active = true;
                if (events_.onCheckpointChanged) events_.onCheckpointChanged(checkpoint_);
            }
            break;
        }
        case RPC_DisableCheckpoint: {
            checkpoint_.active = false;
            if (events_.onCheckpointChanged) events_.onCheckpointChanged(checkpoint_);
            break;
        }
        case RPC_Create3DTextLabel: {
            TextLabel3D lbl{};
            uint8_t los = 0;
            if (bs.Read<uint16_t>(lbl.id) && bs.Read<uint32_t>(lbl.colorRgba) &&
                bs.Read<float>(lbl.position.x) && bs.Read<float>(lbl.position.y) && bs.Read<float>(lbl.position.z) &&
                bs.Read<float>(lbl.drawDistance) && bs.Read<uint8_t>(los) &&
                bs.Read<uint16_t>(lbl.attachedPlayerId) && bs.Read<uint16_t>(lbl.attachedVehicleId)) {
                lbl.testLos = (los != 0);
                StringCompressor::Instance().DecodeString(lbl.text, 2048, bs);
                textLabels_[lbl.id] = lbl;
                if (events_.onTextLabelCreated) events_.onTextLabelCreated(lbl);
            }
            break;
        }
        case RPC_Delete3DTextLabel: {
            uint16_t lid = 0;
            if (bs.Read<uint16_t>(lid)) {
                textLabels_.erase(lid);
                if (events_.onTextLabelDestroyed) events_.onTextLabelDestroyed(lid);
            }
            break;
        }
        case RPC_DisplayGameText: {
            uint32_t style = 0, timeMs = 3000;
            std::string text;
            if (bs.Read<uint32_t>(style) && bs.Read<uint32_t>(timeMs) && bs.ReadDynStr32(text)) {
                if (events_.onGameText) events_.onGameText(style, timeMs, text);
            }
            break;
        }
        case RPC_ShowTextDraw: {
            TextDrawData td{};
            if (ReadServerShowTextDraw(bs, td)) {
                textDraws_[td.id] = std::move(td);
                if (events_.onTextDrawsChanged) events_.onTextDrawsChanged();
            }
            break;
        }
        case RPC_TextDrawHideForPlayer: {
            uint16_t tdId = 0;
            if (bs.Read<uint16_t>(tdId)) {
                textDraws_.erase(tdId);
                if (events_.onTextDrawsChanged) events_.onTextDrawsChanged();
            }
            break;
        }
        case RPC_TextDrawSetString: {
            uint16_t tdId = 0;
            std::string text;
            if (bs.Read<uint16_t>(tdId) && bs.ReadDynStr16(text)) {
                auto it = textDraws_.find(tdId);
                if (it != textDraws_.end()) {
                    it->second.text = std::move(text);
                    if (events_.onTextDrawsChanged) events_.onTextDrawsChanged();
                }
            }
            break;
        }
        case RPC_SelectTextDraw: {
            bool enable = false;
            uint32_t hoverRgba = 0xFFFF00FFu;
            if (bs.Read<bool>(enable)) {
                bs.Read<uint32_t>(hoverRgba);
                textDrawSelect_.active = enable;
                textDrawSelect_.hoverColorRgba = hoverRgba;
                if (events_.onTextDrawSelectChanged) events_.onTextDrawSelectChanged(textDrawSelect_);
                if (events_.onTextDrawsChanged) events_.onTextDrawsChanged();
            }
            break;
        }
        case RPC_PlayAudioStream: {
            AudioStreamState a{};
            uint8_t usePos = 0;
            if (bs.ReadDynStr8(a.url)) {
                bs.Read<float>(a.position.x);
                bs.Read<float>(a.position.y);
                bs.Read<float>(a.position.z);
                bs.Read<float>(a.radius);
                bs.Read<uint8_t>(usePos);
                a.usePos = (usePos != 0);
                a.playing = !a.url.empty();
                audioStream_ = a;
                if (events_.onAudioStream) events_.onAudioStream(audioStream_);
            }
            break;
        }
        case RPC_StopAudioStream: {
            audioStream_.playing = false;
            audioStream_.url.clear();
            if (events_.onAudioStream) events_.onAudioStream(audioStream_);
            break;
        }
        case RPC_ClientCheck:
            HandleRpcClientCheck(bs);
            break;
        case RPC_ForceClassSelection:
            inClassSelection_ = true;
            RequestClass(currentClassId_);
            break;
        case RPC_SetPlayerPos: {
            Vec3 pos{};
            if (bs.Read<float>(pos.x) && bs.Read<float>(pos.y) && bs.Read<float>(pos.z)) {
                localOnFoot_.position = pos;
                if (events_.onSetLocalPos) events_.onSetLocalPos(pos, false);
            }
            break;
        }
        case RPC_SetPlayerPosFindZ: {
            Vec3 pos{};
            if (bs.Read<float>(pos.x) && bs.Read<float>(pos.y) && bs.Read<float>(pos.z)) {
                localOnFoot_.position = pos;
                if (events_.onSetLocalPos) events_.onSetLocalPos(pos, true);
            }
            break;
        }
        case RPC_SetPlayerFacingAngle: {
            float angle = 0.0f;
            if (bs.Read<float>(angle) && events_.onSetLocalFacingAngle) {
                events_.onSetLocalFacingAngle(angle);
            }
            break;
        }
        case RPC_SetPlayerHealth: {
            float hp = 100.0f;
            if (bs.Read<float>(hp) && events_.onSetLocalHealth) {
                events_.onSetLocalHealth(hp);
            }
            break;
        }
        case RPC_SetPlayerArmour: {
            float arm = 0.0f;
            if (bs.Read<float>(arm) && events_.onSetLocalArmour) {
                events_.onSetLocalArmour(arm);
            }
            break;
        }
        case RPC_GivePlayerMoney: {
            int32_t delta = 0;
            if (bs.Read<int32_t>(delta)) {
                localMoney_ += delta;
                if (events_.onMoneyChanged) events_.onMoneyChanged(delta, false);
            }
            break;
        }
        case RPC_ResetPlayerMoney: {
            localMoney_ = 0;
            if (events_.onMoneyChanged) events_.onMoneyChanged(0, true);
            break;
        }
        case RPC_SetPlayerName: {
            uint16_t pid = 0;
            std::string newName;
            uint8_t success = 0;
            if (bs.Read<uint16_t>(pid) && bs.ReadDynStr8(newName) && bs.Read<uint8_t>(success) && success) {
                if (pid == settings_.localPlayerId) {
                    nickname_ = newName;
                } else {
                    auto it = players_.find(pid);
                    if (it != players_.end()) it->second.name = newName;
                }
            }
            break;
        }
        case RPC_SetPlayerColor: {
            uint16_t pid = 0;
            uint32_t color = 0xFFFFFFFF;
            if (bs.Read<uint16_t>(pid) && bs.Read<uint32_t>(color)) {
                if (pid == settings_.localPlayerId) {
                    localColor_ = color;
                } else {
                    auto it = players_.find(pid);
                    if (it != players_.end()) it->second.color = color;
                }
            }
            break;
        }
        case RPC_SetPlayerSkin: {
            uint32_t pid = 0, skin = 0;
            if (bs.Read<uint32_t>(pid) && bs.Read<uint32_t>(skin)) {
                if (pid == settings_.localPlayerId) {
                    spawnInfo_.skin = skin;
                    if (events_.onSetLocalSkin) events_.onSetLocalSkin(skin);
                } else {
                    auto it = players_.find(static_cast<uint16_t>(pid));
                    if (it != players_.end()) {
                        it->second.skin = skin;
                        if (it->second.streamedIn && events_.onRemotePlayerStreamIn) {
                            events_.onRemotePlayerStreamIn(it->second);
                        }
                    }
                }
            }
            break;
        }
        case RPC_SetPlayerInterior: {
            uint8_t interior = 0;
            if (bs.Read<uint8_t>(interior) && events_.onSetLocalInterior) {
                events_.onSetLocalInterior(interior);
            }
            break;
        }
        case RPC_SetPlayerVelocity: {
            Vec3 vel{};
            if (bs.Read<float>(vel.x) && bs.Read<float>(vel.y) && bs.Read<float>(vel.z) &&
                events_.onSetLocalVelocity) {
                events_.onSetLocalVelocity(vel);
            }
            break;
        }
        case RPC_TogglePlayerControllable: {
            uint8_t enable = 1;
            if (bs.Read<uint8_t>(enable) && events_.onToggleControllable) {
                events_.onToggleControllable(enable != 0);
            }
            break;
        }
        case RPC_GivePlayerWeapon: {
            uint32_t wep = 0, ammo = 0;
            if (bs.Read<uint32_t>(wep) && bs.Read<uint32_t>(ammo) && events_.onGiveLocalWeapon) {
                events_.onGiveLocalWeapon(wep, ammo);
            }
            break;
        }
        case RPC_ResetPlayerWeapons:
            if (events_.onResetLocalWeapons) events_.onResetLocalWeapons();
            break;
        case RPC_SetGravity: {
            float g = 0.008f;
            if (bs.Read<float>(g)) {
                settings_.gravity = g;
                if (events_.onSetGravity) events_.onSetGravity(g);
            }
            break;
        }
        case RPC_SetCameraPos: {
            Vec3 pos{};
            if (bs.Read<float>(pos.x) && bs.Read<float>(pos.y) && bs.Read<float>(pos.z) &&
                events_.onSetCameraPos) {
                events_.onSetCameraPos(pos);
            }
            break;
        }
        case RPC_SetCameraLookAt: {
            Vec3 look{};
            uint8_t cutType = 2;
            if (bs.Read<float>(look.x) && bs.Read<float>(look.y) && bs.Read<float>(look.z)) {
                bs.Read<uint8_t>(cutType);
                if (events_.onSetCameraLookAt) events_.onSetCameraLookAt(look, cutType);
            }
            break;
        }
        case RPC_SetCameraBehindPlayer:
            if (events_.onSetCameraBehindPlayer) events_.onSetCameraBehindPlayer();
            break;
        case RPC_SetWorldTime: {
            uint8_t hour = 12;
            if (bs.Read<uint8_t>(hour)) {
                settings_.worldTime = hour;
                if (events_.onSetTime) events_.onSetTime(hour, 0);
            }
            break;
        }
        case RPC_SetPlayerTime: {
            uint8_t hour = 12, minute = 0;
            if (bs.Read<uint8_t>(hour) && bs.Read<uint8_t>(minute)) {
                settings_.worldTime = hour;
                if (events_.onSetTime) events_.onSetTime(hour, minute);
            }
            break;
        }
        case RPC_SetWeather: {
            uint8_t w = 10;
            if (bs.Read<uint8_t>(w)) {
                settings_.weather = w;
                if (events_.onSetWeather) events_.onSetWeather(w);
            }
            break;
        }
        case RPC_SetVehiclePos: {
            uint16_t vid = INVALID_VEHICLE_ID;
            Vec3 pos{};
            if (bs.Read<uint16_t>(vid) && bs.Read<float>(pos.x) && bs.Read<float>(pos.y) && bs.Read<float>(pos.z)) {
                auto it = vehicles_.find(vid);
                if (it != vehicles_.end()) {
                    it->second.position = pos;
                    if (events_.onRemoteVehicleSync) events_.onRemoteVehicleSync(it->second);
                }
            }
            break;
        }
        case RPC_SetVehicleZAngle: {
            uint16_t vid = INVALID_VEHICLE_ID;
            float angle = 0.0f;
            if (bs.Read<uint16_t>(vid) && bs.Read<float>(angle)) {
                auto it = vehicles_.find(vid);
                if (it != vehicles_.end()) {
                    it->second.angle = angle;
                    it->second.quaternion = Quat::FromHeadingDegrees(angle);
                    if (events_.onRemoteVehicleSync) events_.onRemoteVehicleSync(it->second);
                }
            }
            break;
        }
        case RPC_SetVehicleHealth: {
            uint16_t vid = INVALID_VEHICLE_ID;
            float hp = 1000.0f;
            if (bs.Read<uint16_t>(vid) && bs.Read<float>(hp)) {
                auto it = vehicles_.find(vid);
                if (it != vehicles_.end()) {
                    it->second.health = hp;
                    if (events_.onRemoteVehicleSync) events_.onRemoteVehicleSync(it->second);
                }
            }
            break;
        }
        case RPC_PutPlayerInVehicle: {
            uint16_t vid = 0;
            uint8_t seat = 0;
            if (bs.Read<uint16_t>(vid) && bs.Read<uint8_t>(seat)) {
                localInVehicle_ = true;
                localIsPassenger_ = (seat != 0);
                if (seat == 0) {
                    localInCar_.vehicleId = vid;
                } else {
                    localPassenger_.vehicleId = vid;
                    localPassenger_.seatId = seat;
                }
                if (events_.onPutLocalInVehicle) events_.onPutLocalInVehicle(vid, seat);
            }
            break;
        }
        case RPC_RemovePlayerFromVehicle:
            localInVehicle_ = false;
            localIsPassenger_ = false;
            if (events_.onRemoveLocalFromVehicle) events_.onRemoveLocalFromVehicle();
            break;
        case RPC_ModelAdd:
            // Acknowledge 0.3.DL model listing & signal readiness
            if (protocol_ == ProtocolVersion::SAMP_03DL && !sentFinishedDownloading_) {
                SendFinishedDownloading();
            }
            break;
        default:
            break;
    }
}

void SampClient::HandleRpcInitGame(BitStream& bs) {
    bs.Read<bool>(settings_.zoneNames);
    bs.Read<bool>(settings_.useCJWalk);
    bs.Read<bool>(settings_.allowWeapons);
    bs.Read<bool>(settings_.limitGlobalChatRadius);
    bs.Read<float>(settings_.globalChatRadius);
    bs.Read<bool>(settings_.stuntBonus);
    bs.Read<float>(settings_.nametagDrawDist);
    bs.Read<bool>(settings_.disableEnterExits);
    bs.Read<bool>(settings_.nametagLOS);
    bs.Read<bool>(settings_.manualVehEngineAndLight);
    bs.Read<uint32_t>(settings_.spawnsAvailable);
    bs.Read<uint16_t>(settings_.localPlayerId);
    bs.Read<bool>(settings_.showPlayerTags);
    bs.Read<uint32_t>(settings_.showPlayerMarkers);
    bs.Read<uint8_t>(settings_.worldTime);
    bs.Read<uint8_t>(settings_.weather);
    bs.Read<float>(settings_.gravity);
    bs.Read<bool>(settings_.lanMode);
    bs.Read<uint32_t>(settings_.deathDropMoney);
    bs.Read<bool>(settings_.instagib);
    bs.Read<uint32_t>(settings_.onFootRate);
    bs.Read<uint32_t>(settings_.inCarRate);
    bs.Read<uint32_t>(settings_.weaponRate);
    bs.Read<uint32_t>(settings_.multiplier);
    bs.Read<uint32_t>(settings_.lagCompMode);
    bs.ReadDynStr8(settings_.serverName);
    bs.ReadBytes(settings_.vehicleModels.data(), settings_.vehicleModels.size());
    bs.Read<uint32_t>(settings_.vehicleFriendlyFire);

    SetState(ConnectionState::CONNECTED, "Connected to " + settings_.serverName);
    EmitChat(0xA9C4E4FF, "Connected to " + settings_.serverName);

    if (events_.onInitGame) events_.onInitGame(settings_);
    if (events_.onSetTime) events_.onSetTime(settings_.worldTime, 0);
    if (events_.onSetWeather) events_.onSetWeather(settings_.weather);
    if (events_.onSetGravity) events_.onSetGravity(settings_.gravity);

    // On 0.3.DL-R1 servers, notify server that download phase is complete so spawn/class selection opens.
    if (protocol_ == ProtocolVersion::SAMP_03DL) {
        SendFinishedDownloading();
    }

    RequestScoreboardUpdate();

    if (!autoClassRequested_) {
        autoClassRequested_ = true;
        inClassSelection_ = true;
        RequestClass(0);
    }
}

void SampClient::HandleRpcServerJoin(BitStream& bs) {
    uint16_t playerId = INVALID_PLAYER_ID;
    uint32_t color = 0xFFFFFFFF;
    uint8_t isNpc = 0;
    std::string name;
    if (!bs.Read<uint16_t>(playerId) || !bs.Read<uint32_t>(color) ||
        !bs.Read<uint8_t>(isNpc) || !bs.ReadDynStr8(name)) {
        return;
    }
    auto& p = players_[playerId];
    p.playerId = playerId;
    p.name = name;
    p.color = color;
    p.isNpc = isNpc;

    if (events_.onPlayerConnectionChanged) {
        events_.onPlayerConnectionChanged(playerId, name, true);
    }
}

void SampClient::HandleRpcServerQuit(BitStream& bs) {
    uint16_t playerId = INVALID_PLAYER_ID;
    uint8_t reason = 0;
    if (!bs.Read<uint16_t>(playerId)) return;
    bs.Read<uint8_t>(reason);

    std::string name;
    auto it = players_.find(playerId);
    if (it != players_.end()) {
        name = it->second.name;
        if (it->second.streamedIn && events_.onRemotePlayerStreamOut) {
            events_.onRemotePlayerStreamOut(playerId);
        }
        players_.erase(it);
    }
    if (events_.onPlayerConnectionChanged) {
        events_.onPlayerConnectionChanged(playerId, name, false);
    }
}

void SampClient::HandleRpcClientMessage(BitStream& bs) {
    uint32_t rgba = 0xFFFFFFFF;
    std::string msg;
    if (!bs.Read<uint32_t>(rgba) || !bs.ReadDynStr32(msg)) return;
    EmitChat(rgba, msg);
}

void SampClient::HandleRpcChat(BitStream& bs) {
    uint16_t playerId = INVALID_PLAYER_ID;
    std::string text;
    if (!bs.Read<uint16_t>(playerId) || !bs.ReadDynStr8(text)) return;

    std::string senderName;
    uint32_t color = 0xFFFFFFFF;
    if (playerId == settings_.localPlayerId) {
        senderName = nickname_;
        color = localColor_;
    } else {
        auto it = players_.find(playerId);
        if (it != players_.end()) {
            senderName = it->second.name;
            color = it->second.color;
        } else {
            senderName = "Player " + std::to_string(playerId);
        }
    }
    EmitChat(color, senderName + ": {FFFFFF}" + text);
}

void SampClient::HandleRpcShowDialog(BitStream& bs) {
    DialogData dlg;
    if (!ReadServerShowDialog(bs, dlg)) return;
    activeDialog_ = dlg;
    if (dlg.dialogId < 0) {
        if (events_.onHideDialog) events_.onHideDialog();
    } else {
        if (events_.onShowDialog) events_.onShowDialog(dlg);
    }
}

void SampClient::HandleRpcUpdateScoresPings(BitStream& bs) {
    while (bs.GetNumberOfUnreadBits() >= 80) {
        uint16_t pid = 0;
        int32_t score = 0;
        uint32_t ping = 0;
        if (!bs.Read<uint16_t>(pid) || !bs.Read<int32_t>(score) || !bs.Read<uint32_t>(ping)) {
            break;
        }
        if (pid == settings_.localPlayerId) {
            localScore_ = score;
        } else {
            auto it = players_.find(pid);
            if (it != players_.end()) {
                it->second.score = score;
                it->second.ping = ping;
            }
        }
    }
    if (events_.onScoreboardUpdated) {
        events_.onScoreboardUpdated(GetScoreboardEntries());
    }
}

void SampClient::HandleRpcRequestClass(BitStream& bs) {
    uint8_t selectable = 0;
    if (!bs.Read<uint8_t>(selectable)) return;
    bs.Read<uint8_t>(spawnInfo_.team);
    bs.Read<uint32_t>(spawnInfo_.skin);
    uint8_t unknown = 0;
    bs.Read<uint8_t>(unknown);
    bs.Read<float>(spawnInfo_.position.x);
    bs.Read<float>(spawnInfo_.position.y);
    bs.Read<float>(spawnInfo_.position.z);
    bs.Read<float>(spawnInfo_.rotation);
    for (int i = 0; i < 3; ++i) bs.Read<uint32_t>(spawnInfo_.weapons[i]);
    for (int i = 0; i < 3; ++i) bs.Read<uint32_t>(spawnInfo_.ammo[i]);

    inClassSelection_ = true;
    if (events_.onClassSelection) {
        events_.onClassSelection(currentClassId_, spawnInfo_);
    }
}

void SampClient::HandleRpcSetSpawnInfo(BitStream& bs) {
    bs.Read<uint8_t>(spawnInfo_.team);
    bs.Read<uint32_t>(spawnInfo_.skin);
    uint8_t unknown = 0;
    bs.Read<uint8_t>(unknown);
    bs.Read<float>(spawnInfo_.position.x);
    bs.Read<float>(spawnInfo_.position.y);
    bs.Read<float>(spawnInfo_.position.z);
    bs.Read<float>(spawnInfo_.rotation);
    for (int i = 0; i < 3; ++i) bs.Read<uint32_t>(spawnInfo_.weapons[i]);
    for (int i = 0; i < 3; ++i) bs.Read<uint32_t>(spawnInfo_.ammo[i]);
}

void SampClient::HandleRpcRequestSpawn(BitStream& bs) {
    uint8_t response = 0;
    if (!bs.Read<uint8_t>(response)) return;
    if (response == 0) {
        EmitChat(0xFF9966FF, "Spawn denied by server.");
        return;
    }

    inClassSelection_ = false;
    BitStream spawnBs;
    peer_.SendRpc(RPC_Spawn, spawnBs, PacketReliability::RELIABLE_ORDERED, 2);

    localOnFoot_.position = spawnInfo_.position;
    localOnFoot_.quaternion = Quat::FromHeadingDegrees(spawnInfo_.rotation);
    localOnFoot_.health = 100;
    localOnFoot_.armour = 0;
    localOnFoot_.velocity = Vec3{0.0f, 0.0f, 0.0f};
    hasLocalOnFoot_ = true;
    localInVehicle_ = false;
    localIsPassenger_ = false;

    SetState(ConnectionState::SPAWNED, "In Game (" + settings_.serverName + ")");
    if (events_.onLocalSpawn) {
        events_.onLocalSpawn(spawnInfo_);
    }
}

void SampClient::HandleRpcWorldPlayerAdd(BitStream& bs) {
    uint16_t pid = INVALID_PLAYER_ID;
    uint8_t team = 255;
    uint32_t skin = 0;
    Vec3 pos{};
    float rot = 0.0f;
    uint32_t color = 0xFFFFFFFF;
    uint8_t fightingStyle = 0;

    if (!bs.Read<uint16_t>(pid) || !bs.Read<uint8_t>(team) || !bs.Read<uint32_t>(skin) ||
        !bs.Read<float>(pos.x) || !bs.Read<float>(pos.y) || !bs.Read<float>(pos.z) ||
        !bs.Read<float>(rot) || !bs.Read<uint32_t>(color)) {
        return;
    }
    bs.Read<uint8_t>(fightingStyle);

    auto& p = players_[pid];
    p.playerId = pid;
    p.team = team;
    p.skin = skin;
    p.color = color;
    p.streamedIn = true;
    p.inVehicle = false;
    p.isPassenger = false;
    p.onFoot.position = pos;
    p.onFoot.quaternion = Quat::FromHeadingDegrees(rot);
    p.lastSyncTimeMs = NowMs();

    if (events_.onRemotePlayerStreamIn) {
        events_.onRemotePlayerStreamIn(p);
    }
}

void SampClient::HandleRpcWorldPlayerRemove(BitStream& bs) {
    uint16_t pid = INVALID_PLAYER_ID;
    if (!bs.Read<uint16_t>(pid)) return;
    auto it = players_.find(pid);
    if (it != players_.end()) {
        it->second.streamedIn = false;
    }
    if (events_.onRemotePlayerStreamOut) {
        events_.onRemotePlayerStreamOut(pid);
    }
}

void SampClient::HandleRpcConnectionRejected(BitStream& bs) {
    uint8_t reason = 0;
    bs.Read<uint8_t>(reason);
    std::string msg;
    switch (reason) {
        case 0: msg = "Connection rejected: Incorrect version"; break;
        case 1: msg = "Connection rejected: Unacceptable NickName"; break;
        case 2: msg = "Connection rejected: Bad mod version"; break;
        case 3: msg = "Connection rejected: Unable to allocate player slot"; break;
        case 4: msg = "Connection rejected: No available player slot"; break;
        default: msg = "Connection rejected by server (code " + std::to_string(reason) + ")"; break;
    }
    peer_.Disconnect(false);
    SetState(ConnectionState::FAILED, msg);
    EmitChat(0xFF6666FF, msg);
}

void SampClient::HandleRpcWorldVehicleAdd(BitStream& bs) {
    uint16_t vehId = INVALID_VEHICLE_ID;
    uint32_t modelId = 400;
    Vec3 pos{};
    float angle = 0.0f;
    uint8_t c1 = 0, c2 = 0;
    float health = 1000.0f;
    uint8_t interior = 0;

    if (!bs.Read<uint16_t>(vehId) || !bs.Read<uint32_t>(modelId) ||
        !bs.Read<float>(pos.x) || !bs.Read<float>(pos.y) || !bs.Read<float>(pos.z) ||
        !bs.Read<float>(angle) || !bs.Read<uint8_t>(c1) || !bs.Read<uint8_t>(c2) ||
        !bs.Read<float>(health) || !bs.Read<uint8_t>(interior)) {
        return;
    }

    auto& v = vehicles_[vehId];
    v.vehicleId = vehId;
    v.modelId = modelId;
    v.position = pos;
    v.angle = angle;
    v.quaternion = Quat::FromHeadingDegrees(angle);
    v.color1 = c1;
    v.color2 = c2;
    v.health = health;
    v.interior = interior;
    v.streamedIn = true;

    if (events_.onRemoteVehicleStreamIn) {
        events_.onRemoteVehicleStreamIn(v);
    }
}

void SampClient::HandleRpcWorldVehicleRemove(BitStream& bs) {
    uint16_t vehId = INVALID_VEHICLE_ID;
    if (!bs.Read<uint16_t>(vehId)) return;
    auto it = vehicles_.find(vehId);
    if (it != vehicles_.end()) {
        vehicles_.erase(it);
        if (events_.onRemoteVehicleStreamOut) {
            events_.onRemoteVehicleStreamOut(vehId);
        }
    }
}

void SampClient::HandleRpcCreateObject(BitStream& bs) {
    WorldObjectState obj{};
    if (!bs.Read<uint16_t>(obj.objectId) || !bs.Read<int32_t>(obj.modelId) ||
        !bs.Read<float>(obj.position.x) || !bs.Read<float>(obj.position.y) || !bs.Read<float>(obj.position.z) ||
        !bs.Read<float>(obj.rotation.x) || !bs.Read<float>(obj.rotation.y) || !bs.Read<float>(obj.rotation.z)) {
        return;
    }
    bs.Read<float>(obj.drawDistance);
    objects_[obj.objectId] = obj;
    if (events_.onObjectCreated) {
        events_.onObjectCreated(obj);
    }
}

void SampClient::HandleRpcClientCheck(BitStream& bs) {
    uint8_t type = 0;
    uint32_t address = 0;
    uint16_t offset = 0;
    uint16_t count = 0;
    if (!bs.Read<uint8_t>(type) || !bs.Read<uint32_t>(address)) return;
    bs.Read<uint16_t>(offset);
    bs.Read<uint16_t>(count);

    BitStream reply;
    reply.Write<uint8_t>(type);
    reply.Write<uint32_t>(address);
    reply.Write<uint8_t>(0);
    peer_.SendRpc(RPC_ClientCheck, reply, PacketReliability::RELIABLE_ORDERED, 2);
}

void SampClient::OnPacketReceived(uint8_t packetId, BitStream& bs) {
    uint64_t now = NowMs();
    switch (packetId) {
        case ID_PLAYER_SYNC:
            HandlePacketPlayerSync(bs, now);
            break;
        case ID_VEHICLE_SYNC:
            HandlePacketVehicleSync(bs, now);
            break;
        case ID_PASSENGER_SYNC:
            HandlePacketPassengerSync(bs, now);
            break;
        case ID_AIM_SYNC:
            HandlePacketAimSync(bs, now);
            break;
        case ID_MARKERS_SYNC:
            HandlePacketMarkersSync(bs);
            break;
        default:
            break;
    }
}

void SampClient::HandlePacketPlayerSync(BitStream& bs, uint64_t nowMs) {
    uint16_t playerId = INVALID_PLAYER_ID;
    OnFootSyncData sync{};
    if (!ReadServerOnFootSync(bs, playerId, sync)) return;

    auto& p = players_[playerId];
    p.playerId = playerId;
    p.inVehicle = false;
    p.isPassenger = false;
    p.onFoot = sync;
    p.lastSyncTimeMs = nowMs;

    if (events_.onRemotePlayerSync) {
        events_.onRemotePlayerSync(p);
    }
}

void SampClient::HandlePacketVehicleSync(BitStream& bs, uint64_t nowMs) {
    uint16_t playerId = INVALID_PLAYER_ID;
    InCarSyncData sync{};
    if (!ReadServerInCarSync(bs, playerId, sync)) return;

    auto& p = players_[playerId];
    p.playerId = playerId;
    p.inVehicle = true;
    p.isPassenger = false;
    p.inCar = sync;
    p.onFoot.position = sync.position;
    p.lastSyncTimeMs = nowMs;

    auto vit = vehicles_.find(sync.vehicleId);
    if (vit != vehicles_.end()) {
        vit->second.position = sync.position;
        vit->second.quaternion = sync.quaternion;
        vit->second.angle = sync.quaternion.ToHeadingDegrees();
        vit->second.velocity = sync.velocity;
        vit->second.health = sync.carHealth;
        if (events_.onRemoteVehicleSync) {
            events_.onRemoteVehicleSync(vit->second);
        }
    }

    if (events_.onRemotePlayerSync) {
        events_.onRemotePlayerSync(p);
    }
}

void SampClient::HandlePacketPassengerSync(BitStream& bs, uint64_t nowMs) {
    uint16_t playerId = INVALID_PLAYER_ID;
    PassengerSyncData sync{};
    if (!ReadServerPassengerSync(bs, playerId, sync)) return;

    auto& p = players_[playerId];
    p.playerId = playerId;
    p.inVehicle = true;
    p.isPassenger = true;
    p.passenger = sync;
    p.onFoot.position = sync.position;
    p.lastSyncTimeMs = nowMs;

    if (events_.onRemotePlayerSync) {
        events_.onRemotePlayerSync(p);
    }
}

void SampClient::HandlePacketAimSync(BitStream& bs, uint64_t /*nowMs*/) {
    uint8_t packetId = 0;
    uint16_t playerId = INVALID_PLAYER_ID;
    if (!bs.Read<uint8_t>(packetId) || !bs.Read<uint16_t>(playerId)) return;
    AimSyncData aim{};
    uint8_t zoomWep = 0;
    if (!bs.Read<uint8_t>(aim.camMode) ||
        !bs.Read<float>(aim.camFront.x) || !bs.Read<float>(aim.camFront.y) || !bs.Read<float>(aim.camFront.z) ||
        !bs.Read<float>(aim.camPos.x) || !bs.Read<float>(aim.camPos.y) || !bs.Read<float>(aim.camPos.z) ||
        !bs.Read<float>(aim.aimZ) || !bs.Read<uint8_t>(zoomWep) || !bs.Read<uint8_t>(aim.aspectRatio)) {
        return;
    }
    aim.camExtZoom = zoomWep & 0x3F;
    aim.weaponState = (zoomWep >> 6) & 0x03;
    auto it = players_.find(playerId);
    if (it != players_.end()) {
        it->second.aim = aim;
    }
}

void SampClient::HandlePacketMarkersSync(BitStream& bs) {
    uint8_t pid = 0;
    uint32_t count = 0;
    if (!bs.Read<uint8_t>(pid) || !bs.Read<uint32_t>(count)) return;
    if (count > MAX_PLAYERS) return;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t playerId = 0;
        bool active = false;
        if (!bs.Read<uint16_t>(playerId) || !bs.Read<bool>(active)) break;
        if (active) {
            int16_t x = 0, y = 0, z = 0;
            if (!bs.Read<int16_t>(x) || !bs.Read<int16_t>(y) || !bs.Read<int16_t>(z)) break;
            auto it = players_.find(playerId);
            if (it != players_.end() && !it->second.streamedIn) {
                it->second.onFoot.position = Vec3{
                    static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)
                };
            }
        }
    }
}

} // namespace nana::net
