#include "raknet_peer.h"
#include "samp_crypto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace nana::net {

namespace {

constexpr uint64_t HANDSHAKE_RETRY_MS = 500;
constexpr uint64_t HANDSHAKE_TIMEOUT_MS = 10000;
constexpr uint64_t CONNECTION_TIMEOUT_MS = 15000;
constexpr uint64_t PING_INTERVAL_MS = 2000;
constexpr uint64_t ACK_FLUSH_INTERVAL_MS = 30;
constexpr uint64_t RESEND_INTERVAL_MS = 250;
constexpr size_t MAX_SINGLE_FRAME_PAYLOAD_BYTES = 480;

inline bool IsReliable(PacketReliability r) {
    return r == PacketReliability::RELIABLE ||
           r == PacketReliability::RELIABLE_ORDERED ||
           r == PacketReliability::RELIABLE_SEQUENCED;
}

inline bool HasOrdering(PacketReliability r) {
    return r == PacketReliability::UNRELIABLE_SEQUENCED ||
           r == PacketReliability::RELIABLE_ORDERED ||
           r == PacketReliability::RELIABLE_SEQUENCED;
}

inline bool SeqLessThan16(uint16_t a, uint16_t b) {
    return static_cast<int16_t>(a - b) < 0;
}

} // namespace

RakNetPeer::RakNetPeer() = default;

RakNetPeer::~RakNetPeer() {
    Disconnect(false);
}

uint64_t RakNetPeer::NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void RakNetPeer::EmitLog(const std::string& msg) {
    if (callbacks_.onLog) {
        callbacks_.onLog(msg);
    }
}

void RakNetPeer::ResetSessionState() {
    nextMessageNumber_ = 0;
    std::memset(nextOrderingIndex_, 0, sizeof(nextOrderingIndex_));
    std::memset(nextSequencedIndex_, 0, sizeof(nextSequencedIndex_));
    nextSplitPacketId_ = 0;
    receivedCookie_ = 0;
    lastPingMs_ = 0;
    resendQueue_.clear();
    pendingAcks_.clear();
    recentReceivedMessages_.clear();
    hasHighestReceived_ = false;
    highestReceivedMsg_ = 0;
    splitPackets_.clear();
}

bool RakNetPeer::Connect(const std::string& host, uint16_t port, const std::string& password) {
    Disconnect(false);
    ResetSessionState();

    if (host.empty() || port == 0) {
        EmitLog("RakNetPeer: invalid host or port");
        return false;
    }

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || res == nullptr) {
        EmitLog("RakNetPeer: failed to resolve host " + host);
        return false;
    }

    auto* sin = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    serverAddrNetOrder_ = sin->sin_addr.s_addr;
    serverPort_ = port;
    serverHost_ = host;
    password_ = password;
    freeaddrinfo(res);

    sockFd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockFd_ < 0) {
        EmitLog("RakNetPeer: failed to create UDP socket");
        return false;
    }

    int flags = fcntl(sockFd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockFd_, F_SETFL, flags | O_NONBLOCK);
    }

    // Bind ephemeral local port
    struct sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = 0;
    bind(sockFd_, reinterpret_cast<struct sockaddr*>(&localAddr), sizeof(localAddr));

    uint64_t now = NowMs();
    handshakeStartMs_ = now;
    lastHandshakeSendMs_ = now;
    lastPacketRecvMs_ = now;
    lastAckFlushMs_ = now;
    lastPingSendMs_ = now;
    state_ = LinkState::REQUESTING_COOKIE;

    EmitLog("RakNetPeer: connecting to " + host + ":" + std::to_string(port));
    SendOpenConnectionRequest(0);
    return true;
}

void RakNetPeer::Disconnect(bool sendNotification) {
    if (sockFd_ >= 0) {
        if (sendNotification && (state_ == LinkState::CONNECTED || state_ == LinkState::AUTHENTICATING)) {
            BitStream bs;
            bs.Write<uint8_t>(ID_DISCONNECTION_NOTIFICATION);
            SendPacket(bs, PacketReliability::RELIABLE, 0);
            FlushPendingAcks();
        }
        close(sockFd_);
        sockFd_ = -1;
    }
    state_ = LinkState::CLOSED;
    ResetSessionState();
}

void RakNetPeer::TransitionDisconnect(uint8_t reasonId, const std::string& text) {
    if (sockFd_ >= 0) {
        close(sockFd_);
        sockFd_ = -1;
    }
    state_ = LinkState::CLOSED;
    ResetSessionState();
    if (callbacks_.onDisconnected) {
        callbacks_.onDisconnected(reasonId, text);
    }
}

bool RakNetPeer::SendRawEncrypted(const uint8_t* data, size_t len) {
    if (sockFd_ < 0 || data == nullptr || len == 0) return false;
    std::vector<uint8_t> encrypted = SampCrypto::EncryptDatagram(data, len, serverPort_);

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = serverAddrNetOrder_;
    dest.sin_port = htons(serverPort_);

    ssize_t sent = sendto(sockFd_, encrypted.data(), encrypted.size(), 0,
                          reinterpret_cast<const struct sockaddr*>(&dest), sizeof(dest));
    return sent == static_cast<ssize_t>(encrypted.size());
}

void RakNetPeer::SendOpenConnectionRequest(uint16_t cookieField) {
    uint8_t req[3];
    req[0] = ID_OPEN_CONNECTION_REQUEST;
    std::memcpy(req + 1, &cookieField, sizeof(uint16_t));
    SendRawEncrypted(req, sizeof(req));
}

void RakNetPeer::SendConnectionRequest() {
    BitStream bs;
    bs.Write<uint8_t>(ID_CONNECTION_REQUEST);
    if (!password_.empty()) {
        bs.WriteBytes(password_.data(), password_.size());
    }
    SendPacket(bs, PacketReliability::RELIABLE, 0);
}

void RakNetPeer::SendAuthResponse(const std::string& challenge) {
    std::string response = SampCrypto::LookupAuthResponse(challenge);
    if (response.empty()) {
        EmitLog("RakNetPeer: unknown ID_AUTH_KEY challenge: " + challenge);
        TransitionDisconnect(ID_CONNECTION_ATTEMPT_FAILED, "Unsupported server auth challenge");
        return;
    }
    BitStream bs;
    bs.Write<uint8_t>(ID_AUTH_KEY);
    uint8_t len = static_cast<uint8_t>(response.size());
    bs.Write<uint8_t>(len);
    bs.WriteBytes(response.data(), len);
    SendPacket(bs, PacketReliability::RELIABLE, 0);
}

void RakNetPeer::SendInternalPing() {
    BitStream bs;
    bs.Write<uint8_t>(ID_INTERNAL_PING);
    uint32_t now32 = static_cast<uint32_t>(NowMs() & 0xFFFFFFFFu);
    bs.Write<uint32_t>(now32);
    SendPacket(bs, PacketReliability::UNRELIABLE, 0);
}

void RakNetPeer::SendConnectedPong(uint32_t clientPingTime) {
    BitStream bs;
    bs.Write<uint8_t>(ID_CONNECTED_PONG);
    bs.Write<uint32_t>(clientPingTime);
    uint32_t now32 = static_cast<uint32_t>(NowMs() & 0xFFFFFFFFu);
    bs.Write<uint32_t>(now32);
    SendPacket(bs, PacketReliability::UNRELIABLE, 0);
}

void RakNetPeer::WriteAckRangeList(BitStream& bs, const std::vector<uint16_t>& sortedAckIds) {
    struct Range {
        uint16_t minVal;
        uint16_t maxVal;
    };
    std::vector<Range> ranges;
    for (uint16_t id : sortedAckIds) {
        if (!ranges.empty() && static_cast<uint16_t>(ranges.back().maxVal + 1) == id &&
            ranges.back().maxVal != 0xFFFF) {
            ranges.back().maxVal = id;
        } else {
            ranges.push_back(Range{id, id});
        }
    }

    uint16_t count = static_cast<uint16_t>(ranges.size());
    bs.WriteCompressed<uint16_t>(count);
    for (const auto& r : ranges) {
        bool single = (r.minVal == r.maxVal);
        bs.Write<bool>(single);
        bs.Write<uint16_t>(r.minVal);
        if (!single) {
            bs.Write<uint16_t>(r.maxVal);
        }
    }
}

bool RakNetPeer::ReadAckRangeList(BitStream& bs, std::vector<uint16_t>& outAckIds) {
    outAckIds.clear();
    uint16_t count = 0;
    if (!bs.ReadCompressed<uint16_t>(count)) return false;
    for (uint16_t i = 0; i < count; ++i) {
        bool single = false;
        uint16_t minVal = 0;
        uint16_t maxVal = 0;
        if (!bs.Read<bool>(single)) return false;
        if (!bs.Read<uint16_t>(minVal)) return false;
        if (single) {
            maxVal = minVal;
        } else {
            if (!bs.Read<uint16_t>(maxVal)) return false;
            if (maxVal < minVal) return false;
        }
        uint32_t span = static_cast<uint32_t>(maxVal) - static_cast<uint32_t>(minVal);
        if (span > 4096) return false;
        for (uint32_t cur = minVal; cur <= maxVal; ++cur) {
            outAckIds.push_back(static_cast<uint16_t>(cur));
        }
    }
    return true;
}

void RakNetPeer::SerializeInternalFrame(BitStream& bs, const OutgoingFrame& frame) {
    bs.Write<uint16_t>(frame.messageNumber);

    uint8_t rel = static_cast<uint8_t>(frame.reliability);
    bs.WriteBits(&rel, 4, true);

    if (HasOrdering(frame.reliability)) {
        uint8_t chan = static_cast<uint8_t>(frame.orderingChannel & 0x1F);
        bs.WriteBits(&chan, 5, true);
        bs.Write<uint16_t>(frame.orderingIndex);
    }

    bool isSplit = (frame.splitPacketCount > 0);
    bs.Write<bool>(isSplit);
    if (isSplit) {
        bs.Write<uint16_t>(frame.splitPacketId);
        bs.WriteCompressed<uint32_t>(frame.splitPacketIndex);
        bs.WriteCompressed<uint32_t>(frame.splitPacketCount);
    }

    bs.WriteCompressed<uint16_t>(frame.dataBitLength);
    size_t byteLen = (frame.dataBitLength + 7u) >> 3;
    bs.WriteAlignedBytes(frame.data.data(), byteLen);
}

void RakNetPeer::SendFrameNow(OutgoingFrame& frame, bool includePendingAcks) {
    BitStream datagram(600);
    if (includePendingAcks && !pendingAcks_.empty()) {
        datagram.Write<bool>(true);
        std::vector<uint16_t> acks(pendingAcks_.begin(), pendingAcks_.end());
        pendingAcks_.clear();
        WriteAckRangeList(datagram, acks);
        lastAckFlushMs_ = NowMs();
    } else {
        datagram.Write<bool>(false);
    }

    SerializeInternalFrame(datagram, frame);
    SendRawEncrypted(datagram.Data(), datagram.GetNumberOfBytesUsed());
    frame.sendAttempts++;
    frame.nextResendMs = NowMs() + RESEND_INTERVAL_MS;
}

void RakNetPeer::FlushPendingAcks() {
    if (pendingAcks_.empty() || sockFd_ < 0) return;
    if (state_ != LinkState::REQUESTING_SESSION &&
        state_ != LinkState::AUTHENTICATING &&
        state_ != LinkState::CONNECTED) {
        pendingAcks_.clear();
        return;
    }

    BitStream datagram(256);
    datagram.Write<bool>(true);
    std::vector<uint16_t> acks(pendingAcks_.begin(), pendingAcks_.end());
    pendingAcks_.clear();
    WriteAckRangeList(datagram, acks);
    SendRawEncrypted(datagram.Data(), datagram.GetNumberOfBytesUsed());
    lastAckFlushMs_ = NowMs();
}

void RakNetPeer::FlushResends(uint64_t nowMs) {
    for (auto& [msgNum, frame] : resendQueue_) {
        if (nowMs >= frame.nextResendMs) {
            SendFrameNow(frame, true);
        }
    }
}

bool RakNetPeer::SendPacket(const BitStream& bs,
                            PacketReliability reliability,
                            uint8_t orderingChannel) {
    if (sockFd_ < 0 || bs.GetNumberOfBitsUsed() == 0) return false;
    if (state_ != LinkState::REQUESTING_SESSION &&
        state_ != LinkState::AUTHENTICATING &&
        state_ != LinkState::CONNECTED) {
        return false;
    }

    size_t totalBits = bs.GetNumberOfBitsUsed();
    size_t totalBytes = bs.GetNumberOfBytesUsed();
    uint8_t chan = static_cast<uint8_t>(orderingChannel & 0x1F);

    if (totalBytes <= MAX_SINGLE_FRAME_PAYLOAD_BYTES) {
        OutgoingFrame frame;
        frame.messageNumber = nextMessageNumber_++;
        frame.reliability = reliability;
        frame.orderingChannel = chan;
        if (reliability == PacketReliability::RELIABLE_ORDERED) {
            frame.orderingIndex = nextOrderingIndex_[chan]++;
        } else if (HasOrdering(reliability)) {
            frame.orderingIndex = nextSequencedIndex_[chan]++;
        }
        frame.data.assign(bs.Data(), bs.Data() + totalBytes);
        frame.dataBitLength = static_cast<uint16_t>(totalBits);

        SendFrameNow(frame, true);
        if (IsReliable(reliability)) {
            resendQueue_[frame.messageNumber] = std::move(frame);
        }
        return true;
    }

    // Split packet into chunks
    PacketReliability splitRel = reliability;
    if (splitRel == PacketReliability::UNRELIABLE) splitRel = PacketReliability::RELIABLE;
    if (splitRel == PacketReliability::UNRELIABLE_SEQUENCED) splitRel = PacketReliability::RELIABLE_SEQUENCED;

    uint16_t splitId = nextSplitPacketId_++;
    uint16_t ordIdx = 0;
    if (splitRel == PacketReliability::RELIABLE_ORDERED) {
        ordIdx = nextOrderingIndex_[chan]++;
    } else if (HasOrdering(splitRel)) {
        ordIdx = nextSequencedIndex_[chan]++;
    }
    uint32_t splitCount = static_cast<uint32_t>(
        (totalBytes + MAX_SINGLE_FRAME_PAYLOAD_BYTES - 1) / MAX_SINGLE_FRAME_PAYLOAD_BYTES);

    for (uint32_t i = 0; i < splitCount; ++i) {
        size_t offsetBytes = i * MAX_SINGLE_FRAME_PAYLOAD_BYTES;
        size_t chunkBytes = std::min(MAX_SINGLE_FRAME_PAYLOAD_BYTES, totalBytes - offsetBytes);
        size_t chunkBits = (i + 1 == splitCount)
                               ? (totalBits - offsetBytes * 8)
                               : (chunkBytes * 8);

        OutgoingFrame frame;
        frame.messageNumber = nextMessageNumber_++;
        frame.reliability = splitRel;
        frame.orderingChannel = chan;
        frame.orderingIndex = ordIdx;
        frame.splitPacketId = splitId;
        frame.splitPacketIndex = i;
        frame.splitPacketCount = splitCount;
        frame.data.assign(bs.Data() + offsetBytes, bs.Data() + offsetBytes + chunkBytes);
        frame.dataBitLength = static_cast<uint16_t>(chunkBits);

        SendFrameNow(frame, i == 0);
        resendQueue_[frame.messageNumber] = std::move(frame);
    }
    return true;
}

bool RakNetPeer::SendRpc(uint8_t rpcId,
                         const BitStream& params,
                         PacketReliability reliability,
                         uint8_t orderingChannel) {
    BitStream rpcStream(params.GetNumberOfBytesUsed() + 16);
    rpcStream.Write<uint8_t>(ID_RPC);
    rpcStream.Write<uint8_t>(rpcId);
    uint32_t bitLen = static_cast<uint32_t>(params.GetNumberOfBitsUsed());
    rpcStream.WriteCompressed<uint32_t>(bitLen);
    if (bitLen > 0) {
        rpcStream.WriteBits(params.Data(), bitLen, false);
    } else {
        rpcStream.WriteCompressed<uint32_t>(0);
    }
    return SendPacket(rpcStream, reliability, orderingChannel);
}

bool RakNetPeer::IsDuplicateMessage(uint16_t messageNumber) {
    if (!hasHighestReceived_) return false;
    if (recentReceivedMessages_.count(messageNumber) > 0) return true;
    // Anything more than 2048 behind highestReceived is considered stale/duplicate
    int16_t diff = static_cast<int16_t>(messageNumber - highestReceivedMsg_);
    return diff < -2048;
}

void RakNetPeer::RecordReceivedMessage(uint16_t messageNumber) {
    if (!hasHighestReceived_ || SeqLessThan16(highestReceivedMsg_, messageNumber)) {
        highestReceivedMsg_ = messageNumber;
        hasHighestReceived_ = true;
    }
    recentReceivedMessages_.insert(messageNumber);
    if (recentReceivedMessages_.size() > 4096) {
        for (auto it = recentReceivedMessages_.begin(); it != recentReceivedMessages_.end(); ) {
            int16_t diff = static_cast<int16_t>(*it - highestReceivedMsg_);
            if (diff < -2048) {
                it = recentReceivedMessages_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void RakNetPeer::HandleRpcPacket(const uint8_t* data, size_t bitLen) {
    BitStream bs(data, bitLen, true);
    uint8_t packetId = 0;
    if (!bs.Read<uint8_t>(packetId)) return;
    if (packetId == ID_TIMESTAMP) {
        uint32_t ts = 0;
        if (!bs.Read<uint32_t>(ts)) return;
        if (!bs.Read<uint8_t>(packetId)) return;
    }
    if (packetId != ID_RPC) return;

    uint8_t rpcId = 0;
    if (!bs.Read<uint8_t>(rpcId)) return;

    uint32_t rpcBitLength = 0;
    if (!bs.ReadCompressed<uint32_t>(rpcBitLength)) return;

    BitStream rpcPayload;
    if (rpcBitLength > 0 && bs.GetNumberOfUnreadBits() >= rpcBitLength) {
        size_t byteLen = (rpcBitLength + 7u) >> 3;
        std::vector<uint8_t> raw(byteLen, 0);
        if (bs.ReadBits(raw.data(), rpcBitLength, false)) {
            rpcPayload = BitStream(raw.data(), rpcBitLength, true);
        }
    }

    if (callbacks_.onRpcReceived) {
        callbacks_.onRpcReceived(rpcId, rpcPayload);
    }
}

void RakNetPeer::DispatchUserMessage(const uint8_t* data, size_t bitLen, uint64_t nowMs) {
    if (data == nullptr || bitLen < 8) return;
    uint8_t pid = data[0];

    if (pid == ID_RPC || (pid == ID_TIMESTAMP && bitLen >= 48 && data[5] == ID_RPC)) {
        HandleRpcPacket(data, bitLen);
        return;
    }

    if (pid == ID_INTERNAL_PING && bitLen >= 40) {
        uint32_t pingTime = 0;
        std::memcpy(&pingTime, data + 1, sizeof(uint32_t));
        SendConnectedPong(pingTime);
        return;
    }

    if (pid == ID_CONNECTED_PONG && bitLen >= 40) {
        uint32_t sentTime = 0;
        std::memcpy(&sentTime, data + 1, sizeof(uint32_t));
        uint32_t now32 = static_cast<uint32_t>(nowMs & 0xFFFFFFFFu);
        uint32_t rtt = now32 - sentTime;
        if (rtt < 10000) {
            lastPingMs_ = rtt;
        }
        return;
    }

    if (pid == ID_AUTH_KEY) {
        BitStream bs(data, bitLen, true);
        bs.IgnoreBits(8);
        std::string challenge;
        if (bs.ReadDynStr8(challenge)) {
            state_ = LinkState::AUTHENTICATING;
            SendAuthResponse(challenge);
        }
        return;
    }

    if (pid == ID_CONNECTION_REQUEST_ACCEPTED) {
        BitStream bs(data, bitLen, true);
        bs.IgnoreBits(8);
        uint32_t clientIp = 0;
        uint16_t clientPort = 0;
        uint16_t playerIndex = 0;
        uint32_t serverChallenge = 0;
        if (bs.Read<uint32_t>(clientIp) &&
            bs.Read<uint16_t>(clientPort) &&
            bs.Read<uint16_t>(playerIndex) &&
            bs.Read<uint32_t>(serverChallenge)) {
            state_ = LinkState::CONNECTED;
            lastPingSendMs_ = nowMs;

            // Complete RakNet 3-way handshake so the server transitions to CONNECTED
            BitStream confirmBs(16);
            confirmBs.Write<uint8_t>(ID_NEW_INCOMING_CONNECTION);
            confirmBs.Write<uint32_t>(serverAddrNetOrder_);
            confirmBs.Write<uint16_t>(serverPort_);
            SendPacket(confirmBs, PacketReliability::RELIABLE, 0);

            EmitLog("RakNetPeer: connection accepted, playerIndex=" + std::to_string(playerIndex));
            if (callbacks_.onConnected) {
                callbacks_.onConnected(playerIndex, serverChallenge);
            }
        }
        return;
    }

    if (pid == ID_DISCONNECTION_NOTIFICATION) {
        TransitionDisconnect(ID_DISCONNECTION_NOTIFICATION, "Server closed the connection");
        return;
    }
    if (pid == ID_CONNECTION_BANNED) {
        TransitionDisconnect(ID_CONNECTION_BANNED, "You are banned from this server");
        return;
    }
    if (pid == ID_INVALID_PASSWORD) {
        TransitionDisconnect(ID_INVALID_PASSWORD, "Wrong server password");
        return;
    }
    if (pid == ID_NO_FREE_INCOMING_CONNECTIONS) {
        TransitionDisconnect(ID_NO_FREE_INCOMING_CONNECTIONS, "The server is full");
        return;
    }
    if (pid == ID_CONNECTION_LOST) {
        TransitionDisconnect(ID_CONNECTION_LOST, "Connection lost");
        return;
    }

    if (callbacks_.onPacketReceived) {
        BitStream payload(data, bitLen, true);
        callbacks_.onPacketReceived(pid, payload);
    }
}

void RakNetPeer::HandleConnectedDatagram(const uint8_t* data, size_t len, uint64_t nowMs) {
    if (len == 0) return;
    BitStream bs(data, len);

    bool hasAcks = false;
    if (!bs.Read<bool>(hasAcks)) return;

    if (hasAcks) {
        std::vector<uint16_t> acks;
        if (!ReadAckRangeList(bs, acks)) return;
        for (uint16_t ackId : acks) {
            resendQueue_.erase(ackId);
        }
    }

    while (bs.GetNumberOfUnreadBits() >= 16) {
        uint16_t msgNum = 0;
        if (!bs.Read<uint16_t>(msgNum)) break;

        uint8_t relRaw = 0;
        if (!bs.ReadBits(&relRaw, 4, true)) break;
        auto reliability = static_cast<PacketReliability>(relRaw);

        uint8_t orderingChannel = 0;
        uint16_t orderingIndex = 0;
        if (HasOrdering(reliability)) {
            if (!bs.ReadBits(&orderingChannel, 5, true)) break;
            if (!bs.Read<uint16_t>(orderingIndex)) break;
        }

        bool isSplit = false;
        if (!bs.Read<bool>(isSplit)) break;

        uint16_t splitId = 0;
        uint32_t splitIdx = 0;
        uint32_t splitCount = 0;
        if (isSplit) {
            if (!bs.Read<uint16_t>(splitId)) break;
            if (!bs.ReadCompressed<uint32_t>(splitIdx)) break;
            if (!bs.ReadCompressed<uint32_t>(splitCount)) break;
        }

        uint16_t dataBitLen = 0;
        if (!bs.ReadCompressed<uint16_t>(dataBitLen)) break;
        if (dataBitLen == 0) break;

        size_t byteLen = (dataBitLen + 7u) >> 3;
        std::vector<uint8_t> payload(byteLen);
        if (!bs.ReadAlignedBytes(payload.data(), byteLen)) break;

        if (IsReliable(reliability)) {
            pendingAcks_.insert(msgNum);
            if (IsDuplicateMessage(msgNum)) {
                continue;
            }
            RecordReceivedMessage(msgNum);
        }

        if (isSplit) {
            if (splitCount == 0 || splitCount > 256 || splitIdx >= splitCount) {
                continue;
            }
            auto& assembly = splitPackets_[splitId];
            if (assembly.totalCount == 0) {
                assembly.totalCount = splitCount;
                assembly.chunks.resize(splitCount);
                assembly.chunkBitLengths.resize(splitCount, 0);
                assembly.present.resize(splitCount, false);
            }
            if (assembly.totalCount == splitCount && !assembly.present[splitIdx]) {
                assembly.chunks[splitIdx] = std::move(payload);
                assembly.chunkBitLengths[splitIdx] = dataBitLen;
                assembly.present[splitIdx] = true;
                assembly.receivedCount++;
                assembly.lastUpdateMs = nowMs;
            }
            if (assembly.receivedCount == assembly.totalCount) {
                std::vector<uint8_t> full;
                size_t fullBits = 0;
                for (uint32_t i = 0; i < assembly.totalCount; ++i) {
                    full.insert(full.end(), assembly.chunks[i].begin(), assembly.chunks[i].end());
                    fullBits += assembly.chunkBitLengths[i];
                }
                splitPackets_.erase(splitId);
                DispatchUserMessage(full.data(), fullBits, nowMs);
            }
        } else {
            DispatchUserMessage(payload.data(), dataBitLen, nowMs);
        }

        if (state_ == LinkState::CLOSED) {
            break;
        }
    }
}

void RakNetPeer::HandleRawDatagram(const uint8_t* data, size_t len, uint64_t nowMs) {
    if (len == 0) return;
    lastPacketRecvMs_ = nowMs;

    // Offline handshake responses (server -> client are unencrypted in SA-MP 0.3.7)
    if (state_ == LinkState::REQUESTING_COOKIE || state_ == LinkState::OPENING_CONNECTION) {
        uint8_t pid = data[0];
        if (pid == ID_OPEN_CONNECTION_COOKIE && len >= 3) {
            uint16_t cookie = 0;
            std::memcpy(&cookie, data + 1, sizeof(uint16_t));
            receivedCookie_ = cookie;
            state_ = LinkState::OPENING_CONNECTION;
            lastHandshakeSendMs_ = nowMs;
            SendOpenConnectionRequest(static_cast<uint16_t>(cookie ^ SAMP_COOKIE_XOR));
            return;
        }
        if (pid == ID_OPEN_CONNECTION_REPLY && len == 2) {
            state_ = LinkState::REQUESTING_SESSION;
            SendConnectionRequest();
            return;
        }
        if (pid == ID_NO_FREE_INCOMING_CONNECTIONS && len == 2) {
            TransitionDisconnect(ID_NO_FREE_INCOMING_CONNECTIONS, "The server is full");
            return;
        }
        if (pid == ID_CONNECTION_ATTEMPT_FAILED && len == 2) {
            TransitionDisconnect(ID_CONNECTION_ATTEMPT_FAILED, "Connection attempt failed");
            return;
        }
        if (pid == ID_CONNECTION_BANNED && len == 2) {
            TransitionDisconnect(ID_CONNECTION_BANNED, "You are banned from this server");
            return;
        }
        return;
    }

    HandleConnectedDatagram(data, len, nowMs);
}

void RakNetPeer::Tick() {
    if (sockFd_ < 0 || state_ == LinkState::CLOSED) return;

    uint64_t now = NowMs();

    // Drain incoming UDP datagrams
    uint8_t recvBuf[2048];
    struct sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    for (int i = 0; i < 64; ++i) {
        ssize_t n = recvfrom(sockFd_, recvBuf, sizeof(recvBuf), 0,
                             reinterpret_cast<struct sockaddr*>(&from), &fromLen);
        if (n <= 0) {
            break;
        }
        if (from.sin_addr.s_addr != serverAddrNetOrder_ || ntohs(from.sin_port) != serverPort_) {
            continue;
        }
        HandleRawDatagram(recvBuf, static_cast<size_t>(n), now);
        if (state_ == LinkState::CLOSED) return;
    }

    // Offline handshake retries & timeout
    if (state_ == LinkState::REQUESTING_COOKIE || state_ == LinkState::OPENING_CONNECTION) {
        if (now - handshakeStartMs_ > HANDSHAKE_TIMEOUT_MS) {
            TransitionDisconnect(ID_CONNECTION_ATTEMPT_FAILED, "Connection timed out (server did not respond)");
            return;
        }
        if (now - lastHandshakeSendMs_ >= HANDSHAKE_RETRY_MS) {
            lastHandshakeSendMs_ = now;
            if (state_ == LinkState::REQUESTING_COOKIE) {
                SendOpenConnectionRequest(0);
            } else {
                SendOpenConnectionRequest(static_cast<uint16_t>(receivedCookie_ ^ SAMP_COOKIE_XOR));
            }
        }
        return;
    }

    // Connected session timeout
    if (now - lastPacketRecvMs_ > CONNECTION_TIMEOUT_MS) {
        TransitionDisconnect(ID_CONNECTION_LOST, "Lost connection to the server (timeout)");
        return;
    }

    // Reliable packet resends & ACK flush
    FlushResends(now);
    if (!pendingAcks_.empty() && (now - lastAckFlushMs_ >= ACK_FLUSH_INTERVAL_MS)) {
        FlushPendingAcks();
    }

    // Periodic keepalive ping
    if (state_ == LinkState::CONNECTED && (now - lastPingSendMs_ >= PING_INTERVAL_MS)) {
        lastPingSendMs_ = now;
        SendInternalPing();
    }

    // Purge stale incomplete split packets
    for (auto it = splitPackets_.begin(); it != splitPackets_.end(); ) {
        if (now - it->second.lastUpdateMs > 10000) {
            it = splitPackets_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace nana::net
