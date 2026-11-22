#pragma once

#include "bitstream.h"
#include "raknet_types.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nana::net {

/**
 * From-scratch RakNet Legacy (SA-MP 0.3.7) UDP transport & reliability peer.
 *
 * Implements:
 * - SA-MP 0.3.7 outgoing datagram obfuscation & checksum
 * - Offline connection cookie handshake (ID_OPEN_CONNECTION_REQUEST / COOKIE / REPLY)
 * - Connected RakNet reliability layer (ACK RangeList, 4-bit legacy reliability,
 *   ordering channels/indices, split-packet reassembly, reliable resend queue,
 *   duplicate filtering)
 * - ID_CONNECTION_REQUEST, ID_AUTH_KEY challenge/response, ID_CONNECTION_REQUEST_ACCEPTED
 * - ID_INTERNAL_PING / ID_CONNECTED_PONG keepalive and RTT measurement
 * - ID_RPC framing (legacy 8-bit RPC IDs + compressed bit length)
 */
class RakNetPeer {
public:
    enum class LinkState {
        CLOSED,
        REQUESTING_COOKIE,
        OPENING_CONNECTION,
        REQUESTING_SESSION,
        AUTHENTICATING,
        CONNECTED,
    };

    struct Callbacks {
        std::function<void(uint16_t localPlayerIndex, uint32_t serverChallenge)> onConnected;
        std::function<void(uint8_t reasonPacketId, const std::string& reasonText)> onDisconnected;
        std::function<void(uint8_t rpcId, BitStream& payload)> onRpcReceived;
        std::function<void(uint8_t packetId, BitStream& payload)> onPacketReceived;
        std::function<void(const std::string& logLine)> onLog;
    };

    RakNetPeer();
    ~RakNetPeer();

    RakNetPeer(const RakNetPeer&) = delete;
    RakNetPeer& operator=(const RakNetPeer&) = delete;

    void SetCallbacks(Callbacks cb) { callbacks_ = std::move(cb); }

    bool Connect(const std::string& host, uint16_t port, const std::string& password = "");
    void Disconnect(bool sendNotification = true);

    // Must be called periodically (e.g. every 10-20ms or every game frame).
    void Tick();

    bool SendPacket(const BitStream& bs,
                    PacketReliability reliability,
                    uint8_t orderingChannel = 0);

    bool SendRpc(uint8_t rpcId,
                 const BitStream& params,
                 PacketReliability reliability = PacketReliability::RELIABLE_ORDERED,
                 uint8_t orderingChannel = 2);

    LinkState GetState() const { return state_; }
    bool IsConnected() const { return state_ == LinkState::CONNECTED; }
    uint32_t GetPingMs() const { return lastPingMs_; }
    uint16_t GetServerPort() const { return serverPort_; }
    uint32_t GetServerAddressNetOrder() const { return serverAddrNetOrder_; }

    // Helpers exposed for unit testing frame encoding/decoding
    static void WriteAckRangeList(BitStream& bs, const std::vector<uint16_t>& sortedAckIds);
    static bool ReadAckRangeList(BitStream& bs, std::vector<uint16_t>& outAckIds);

private:
    struct OutgoingFrame {
        uint16_t messageNumber = 0;
        PacketReliability reliability = PacketReliability::RELIABLE;
        uint8_t orderingChannel = 0;
        uint16_t orderingIndex = 0;
        uint16_t splitPacketId = 0;
        uint32_t splitPacketIndex = 0;
        uint32_t splitPacketCount = 0;
        std::vector<uint8_t> data;
        uint16_t dataBitLength = 0;
        uint64_t nextResendMs = 0;
        int sendAttempts = 0;
    };

    struct SplitPacketAssembly {
        uint32_t totalCount = 0;
        uint32_t receivedCount = 0;
        std::vector<std::vector<uint8_t>> chunks;
        std::vector<uint16_t> chunkBitLengths;
        std::vector<bool> present;
        uint64_t lastUpdateMs = 0;
    };

    static uint64_t NowMs();

    void ResetSessionState();
    bool SendRawEncrypted(const uint8_t* data, size_t len);
    void SendOpenConnectionRequest(uint16_t cookieField);
    void SendConnectionRequest();
    void SendAuthResponse(const std::string& challenge);
    void SendInternalPing();
    void SendConnectedPong(uint32_t clientPingTime);
    void FlushPendingAcks();
    void FlushResends(uint64_t nowMs);
    void SendFrameNow(OutgoingFrame& frame, bool includePendingAcks);

    static void SerializeInternalFrame(BitStream& bs, const OutgoingFrame& frame);

    void HandleRawDatagram(const uint8_t* data, size_t len, uint64_t nowMs);
    void HandleConnectedDatagram(const uint8_t* data, size_t len, uint64_t nowMs);
    void DispatchUserMessage(const uint8_t* data, size_t bitLen, uint64_t nowMs);
    void HandleRpcPacket(const uint8_t* data, size_t bitLen);
    bool IsDuplicateMessage(uint16_t messageNumber);
    void RecordReceivedMessage(uint16_t messageNumber);

    void EmitLog(const std::string& msg);
    void TransitionDisconnect(uint8_t reasonId, const std::string& text);

    int sockFd_ = -1;
    uint32_t serverAddrNetOrder_ = 0;
    uint16_t serverPort_ = 0;
    std::string serverHost_;
    std::string password_;

    LinkState state_ = LinkState::CLOSED;
    Callbacks callbacks_;

    uint16_t nextMessageNumber_ = 0;
    uint16_t nextOrderingIndex_[32]{};
    uint16_t nextSequencedIndex_[32]{};
    uint16_t nextSplitPacketId_ = 0;

    uint16_t receivedCookie_ = 0;
    uint64_t handshakeStartMs_ = 0;
    uint64_t lastHandshakeSendMs_ = 0;
    uint64_t lastPacketRecvMs_ = 0;
    uint64_t lastPingSendMs_ = 0;
    uint32_t lastPingMs_ = 0;

    std::map<uint16_t, OutgoingFrame> resendQueue_;
    std::set<uint16_t> pendingAcks_;
    uint64_t lastAckFlushMs_ = 0;

    // Sliding window duplicate tracker for received reliable message numbers
    std::set<uint16_t> recentReceivedMessages_;
    bool hasHighestReceived_ = false;
    uint16_t highestReceivedMsg_ = 0;

    std::map<uint16_t, SplitPacketAssembly> splitPackets_;
};

} // namespace nana::net
