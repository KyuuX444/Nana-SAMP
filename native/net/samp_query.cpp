#include "samp_query.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

namespace nana::net {

namespace {

bool ReadU32String(const uint8_t* buf, size_t len, size_t& off, std::string& out) {
    if (off + 4 > len) return false;
    uint32_t slen = 0;
    std::memcpy(&slen, buf + off, 4);
    off += 4;
    if (slen > 512 || off + slen > len) return false;
    out.assign(reinterpret_cast<const char*>(buf + off), slen);
    off += slen;
    return true;
}

bool ReadU8String(const uint8_t* buf, size_t len, size_t& off, std::string& out) {
    if (off + 1 > len) return false;
    uint8_t slen = buf[off++];
    if (off + slen > len) return false;
    out.assign(reinterpret_cast<const char*>(buf + off), slen);
    off += slen;
    return true;
}

bool ResolveIPv4(const std::string& host, uint16_t port, struct sockaddr_in& outAddr) {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        return false;
    }
    std::memcpy(&outAddr, res->ai_addr, sizeof(outAddr));
    freeaddrinfo(res);
    return true;
}

bool ExchangeQuery(const struct sockaddr_in& addr, uint16_t port, char opcode, int timeoutMs,
                   std::vector<uint8_t>& outResp, uint32_t& outPingMs, std::string& outErr) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        outErr = "Failed to open UDP socket";
        return false;
    }

    uint32_t ipNet = addr.sin_addr.s_addr;
    uint8_t ipBytes[4];
    std::memcpy(ipBytes, &ipNet, 4);

    uint8_t packet[11];
    packet[0] = 'S';
    packet[1] = 'A';
    packet[2] = 'M';
    packet[3] = 'P';
    packet[4] = ipBytes[0];
    packet[5] = ipBytes[1];
    packet[6] = ipBytes[2];
    packet[7] = ipBytes[3];
    packet[8] = static_cast<uint8_t>(port & 0xFF);
    packet[9] = static_cast<uint8_t>((port >> 8) & 0xFF);
    packet[10] = static_cast<uint8_t>(opcode);

    auto t0 = std::chrono::steady_clock::now();
    ssize_t sent = sendto(sock, packet, sizeof(packet), 0,
                          reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    if (sent != static_cast<ssize_t>(sizeof(packet))) {
        close(sock);
        outErr = "Failed to send query packet";
        return false;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    if (select(sock + 1, &rfds, nullptr, nullptr, &tv) <= 0) {
        close(sock);
        outErr = "Server did not respond (timeout)";
        return false;
    }

    outResp.resize(4096);
    struct sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    ssize_t n = recvfrom(sock, outResp.data(), outResp.size(), 0,
                         reinterpret_cast<struct sockaddr*>(&from), &fromLen);
    auto t1 = std::chrono::steady_clock::now();
    close(sock);

    if (n < 11 || std::memcmp(outResp.data(), "SAMP", 4) != 0 || outResp[10] != static_cast<uint8_t>(opcode)) {
        outResp.clear();
        outErr = "Invalid SA-MP query response";
        return false;
    }
    outResp.resize(static_cast<size_t>(n));
    outPingMs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    return true;
}

bool ParseInfoPacket(const uint8_t* buf, size_t len, ServerQueryInfo& info) {
    if (len < 16 || std::memcmp(buf, "SAMP", 4) != 0 || buf[10] != 'i') return false;
    size_t off = 11;
    info.hasPassword = buf[off++] != 0;
    std::memcpy(&info.players, buf + off, 2);
    off += 2;
    std::memcpy(&info.maxPlayers, buf + off, 2);
    off += 2;
    if (!ReadU32String(buf, len, off, info.hostname) ||
        !ReadU32String(buf, len, off, info.gamemode) ||
        !ReadU32String(buf, len, off, info.language)) {
        return false;
    }
    info.success = true;
    return true;
}

} // namespace

ServerQueryInfo SampQuery::QueryInfo(const std::string& host, uint16_t port, int timeoutMs) {
    ServerQueryInfo result;
    result.host = host;
    result.port = port;
    if (host.empty() || port == 0) {
        result.error = "Invalid host or port";
        return result;
    }

    struct sockaddr_in addr{};
    if (!ResolveIPv4(host, port, addr)) {
        result.error = "DNS resolution failed";
        return result;
    }

    std::vector<uint8_t> resp;
    uint32_t pingMs = 0;
    if (!ExchangeQuery(addr, port, 'i', timeoutMs, resp, pingMs, result.error)) {
        return result;
    }
    if (!ParseInfoPacket(resp.data(), resp.size(), result)) {
        result.error = "Truncated SA-MP query response";
        return result;
    }
    result.pingMs = pingMs;
    return result;
}

std::map<std::string, std::string> SampQuery::QueryRules(const std::string& host, uint16_t port, int timeoutMs) {
    std::map<std::string, std::string> rules;
    struct sockaddr_in addr{};
    if (host.empty() || port == 0 || !ResolveIPv4(host, port, addr)) return rules;

    std::vector<uint8_t> resp;
    uint32_t pingMs = 0;
    std::string err;
    if (!ExchangeQuery(addr, port, 'r', timeoutMs, resp, pingMs, err) || resp.size() < 13) {
        return rules;
    }
    size_t off = 11;
    uint16_t count = 0;
    std::memcpy(&count, resp.data() + off, 2);
    off += 2;
    for (uint16_t i = 0; i < count; ++i) {
        std::string key, val;
        if (!ReadU8String(resp.data(), resp.size(), off, key) ||
            !ReadU8String(resp.data(), resp.size(), off, val)) {
            break;
        }
        rules[key] = val;
    }
    return rules;
}

std::vector<ServerQueryPlayer> SampQuery::QueryPlayers(const std::string& host, uint16_t port, int timeoutMs) {
    std::vector<ServerQueryPlayer> players;
    struct sockaddr_in addr{};
    if (host.empty() || port == 0 || !ResolveIPv4(host, port, addr)) return players;

    std::vector<uint8_t> resp;
    uint32_t pingMs = 0;
    std::string err;
    if (ExchangeQuery(addr, port, 'd', timeoutMs, resp, pingMs, err) && resp.size() >= 13) {
        size_t off = 11;
        uint16_t count = 0;
        std::memcpy(&count, resp.data() + off, 2);
        off += 2;
        for (uint16_t i = 0; i < count && off < resp.size(); ++i) {
            ServerQueryPlayer p{};
            p.playerId = resp[off++];
            if (!ReadU8String(resp.data(), resp.size(), off, p.nickname)) break;
            if (off + 8 > resp.size()) break;
            std::memcpy(&p.score, resp.data() + off, 4);
            off += 4;
            std::memcpy(&p.ping, resp.data() + off, 4);
            off += 4;
            players.push_back(std::move(p));
        }
        return players;
    }

    // Fallback to basic client list ('c')
    if (ExchangeQuery(addr, port, 'c', timeoutMs, resp, pingMs, err) && resp.size() >= 13) {
        size_t off = 11;
        uint16_t count = 0;
        std::memcpy(&count, resp.data() + off, 2);
        off += 2;
        for (uint16_t i = 0; i < count && off < resp.size(); ++i) {
            ServerQueryPlayer p{};
            p.playerId = static_cast<uint8_t>(i);
            if (!ReadU8String(resp.data(), resp.size(), off, p.nickname)) break;
            if (off + 4 > resp.size()) break;
            std::memcpy(&p.score, resp.data() + off, 4);
            off += 4;
            players.push_back(std::move(p));
        }
    }
    return players;
}

std::vector<ServerQueryInfo> SampQuery::DiscoverLanServers(uint16_t port, int timeoutMs) {
    std::vector<ServerQueryInfo> found;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return found;

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(port);
    bcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    uint8_t packet[11] = {
        'S', 'A', 'M', 'P',
        255, 255, 255, 255,
        static_cast<uint8_t>(port & 0xFF),
        static_cast<uint8_t>((port >> 8) & 0xFF),
        'i'
    };

    auto t0 = std::chrono::steady_clock::now();
    sendto(sock, packet, sizeof(packet), 0,
           reinterpret_cast<const struct sockaddr*>(&bcast), sizeof(bcast));

    auto deadline = t0 + std::chrono::milliseconds(timeoutMs);
    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        auto remMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        struct timeval tv{};
        tv.tv_sec = static_cast<long>(remMs / 1000);
        tv.tv_usec = static_cast<long>((remMs % 1000) * 1000);

        int sel = select(sock + 1, &rfds, nullptr, nullptr, &tv);
        if (sel <= 0) break;

        uint8_t recvBuf[1024];
        struct sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        ssize_t n = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                             reinterpret_cast<struct sockaddr*>(&from), &fromLen);
        if (n >= 16) {
            ServerQueryInfo info;
            if (ParseInfoPacket(recvBuf, static_cast<size_t>(n), info)) {
                char ipStr[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));
                info.host = ipStr;
                info.port = ntohs(from.sin_port);
                info.pingMs = static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count());
                found.push_back(std::move(info));
            }
        }
    }

    close(sock);
    return found;
}

} // namespace nana::net
