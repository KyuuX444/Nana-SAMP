#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nana::net {

struct ServerQueryInfo {
    bool success = false;
    bool hasPassword = false;
    uint16_t players = 0;
    uint16_t maxPlayers = 0;
    uint32_t pingMs = 0;
    std::string host;
    uint16_t port = 0;
    std::string hostname;
    std::string gamemode;
    std::string language;
    std::string error;
};

struct ServerQueryPlayer {
    uint8_t playerId = 0;
    std::string nickname;
    int32_t score = 0;
    int32_t ping = 0;
};

/**
 * Full SA-MP / open.mp UDP Query protocol client:
 *   'i' - Server Info (hostname, gamemode, language, players/maxPlayers, password, ping)
 *   'r' - Server Rules (version, mapname, weather, worldtime, weburl, lagcomp, etc.)
 *   'd' - Detailed Player List (ID, nickname, score, ping) with automatic fallback to 'c'
 *   LAN Broadcast Discovery on 255.255.255.255
 */
class SampQuery {
public:
    static ServerQueryInfo QueryInfo(const std::string& host, uint16_t port, int timeoutMs = 2000);
    static std::map<std::string, std::string> QueryRules(const std::string& host, uint16_t port, int timeoutMs = 2000);
    static std::vector<ServerQueryPlayer> QueryPlayers(const std::string& host, uint16_t port, int timeoutMs = 2000);
    static std::vector<ServerQueryInfo> DiscoverLanServers(uint16_t port = 7777, int timeoutMs = 1200);
};

} // namespace nana::net
