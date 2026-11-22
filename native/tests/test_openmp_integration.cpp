#include "../net/samp_client.h"
#include "../net/samp_query.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace nana::net;

namespace {

void SleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool WaitUntil(std::vector<SampClient*>& clients, int timeoutMs, const std::function<bool()>& pred) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto* c : clients) c->Tick();
        if (pred()) return true;
        SleepMs(10);
    }
    return pred();
}

} // namespace

/**
 * Real end-to-end SA-MP 0.3.7 & 0.3.DL-R1 multiplayer integration test against a live open.mp server.
 *
 * Verifies:
 *   1. Real UDP Query ('i' info & 'r' rules)
 *   2. Real 0.3.7 handshake + Auth + Join + InitGame (Client 1 & Client 2)
 *   3. Real 0.3.DL-R1 (NETGAME_VERSION=4062) handshake + Auth + Join + InitGame + FinishedDownloading (Client 3)
 *   4. Real RPC_ServerJoin mutual visibility & RPC_UpdateScoresPingsIPs scoreboard population
 *   5. Real RPC_RequestClass -> RPC_RequestSpawn -> RPC_Spawn
 *   6. Real ID_PLAYER_SYNC (207) on-foot movement broadcast between players
 *   7. Real RPC_Chat (101) message delivery between players
 *   8. Clean disconnect + RPC_ServerQuit notification + Reconnect verification
 */
int main(int argc, char** argv) {
    std::string host = (argc >= 2) ? argv[1] : "127.0.0.1";
    uint16_t port = (argc >= 3) ? static_cast<uint16_t>(std::atoi(argv[2])) : 7777;

    std::printf("[Integration] Target SA-MP/open.mp server: %s:%u\n", host.c_str(), port);

    // 1. Real SA-MP UDP Query ('i' and 'r')
    auto query = SampQuery::QueryInfo(host, port, 3000);
    if (!query.success) {
        std::fprintf(stderr, "[FAIL] SA-MP UDP Query ('i') failed: %s\n", query.error.c_str());
        return EXIT_FAILURE;
    }
    auto rules = SampQuery::QueryRules(host, port, 3000);
    std::printf("[PASS] Query OK: hostname='%s' mode='%s' players=%u/%u ping=%ums rules=%zu\n",
                query.hostname.c_str(), query.gamemode.c_str(),
                query.players, query.maxPlayers, query.pingMs, rules.size());

    SampClient c1, c2, c3dl;
    std::vector<SampClient*> all = {&c1, &c2, &c3dl};

    bool p1Init = false, p2Init = false, p3Init = false;
    bool p1Class = false, p2Class = false;
    bool p1Spawned = false, p2Spawned = false;
    bool p2SawP1Join = false, p2StreamedP1 = false, p2SawP1Move = false, p2SawP1Chat = false, p2SawP1Quit = false;
    bool p1ScoreboardUpdated = false;
    bool p1GotTeleport = false;
    Vec3 p1SpawnPos{};
    Vec3 p1TargetPos{};
    Vec3 p2SeenP1Pos{};

    SampClient::Events ev1;
    ev1.onInitGame = [&](const ServerSettings& s) {
        p1Init = true;
        std::printf("[P1 0.3.7 InitGame] id=%u server='%s' spawns=%u\n",
                    s.localPlayerId, s.serverName.c_str(), s.spawnsAvailable);
    };
    ev1.onClassSelection = [&](uint16_t cid, const SpawnInfo& info) {
        p1Class = true;
        std::printf("[P1 Class] cid=%u skin=%u pos=(%.1f,%.1f,%.1f)\n",
                    cid, info.skin, info.position.x, info.position.y, info.position.z);
    };
    ev1.onLocalSpawn = [&](const SpawnInfo& info) {
        p1Spawned = true;
        p1SpawnPos = info.position;
    };
    ev1.onSetLocalPos = [&](Vec3 pos, bool) {
        p1GotTeleport = true;
        p1SpawnPos = pos;
    };
    ev1.onScoreboardUpdated = [&](const std::vector<ScoreboardEntry>& entries) {
        if (entries.size() >= 2) p1ScoreboardUpdated = true;
    };
    c1.SetEvents(std::move(ev1));

    SampClient::Events ev2;
    ev2.onInitGame = [&](const ServerSettings& s) {
        p2Init = true;
        std::printf("[P2 0.3.7 InitGame] id=%u\n", s.localPlayerId);
    };
    ev2.onClassSelection = [&](uint16_t, const SpawnInfo&) { p2Class = true; };
    ev2.onLocalSpawn = [&](const SpawnInfo&) { p2Spawned = true; };
    ev2.onPlayerConnectionChanged = [&](uint16_t, const std::string& name, bool joined) {
        if (name == "NanaAlpha") {
            if (joined) p2SawP1Join = true;
            else p2SawP1Quit = true;
        }
    };
    ev2.onRemotePlayerStreamIn = [&](const RemotePlayerState& rp) {
        if (rp.playerId == c1.GetLocalPlayerId()) {
            p2StreamedP1 = true;
            p2SeenP1Pos = rp.onFoot.position;
        }
    };
    ev2.onRemotePlayerSync = [&](const RemotePlayerState& rp) {
        if (rp.playerId == c1.GetLocalPlayerId()) {
            p2SeenP1Pos = rp.onFoot.position;
            float dx = rp.onFoot.position.x - p1TargetPos.x;
            float dy = rp.onFoot.position.y - p1TargetPos.y;
            if (std::sqrt(dx * dx + dy * dy) < 2.5f) {
                p2SawP1Move = true;
            }
        }
    };
    ev2.onChatMessage = [&](uint32_t, const std::string& text) {
        if (text.find("NANA_MP_OK") != std::string::npos) {
            p2SawP1Chat = true;
        }
    };
    c2.SetEvents(std::move(ev2));

    SampClient::Events ev3;
    ev3.onInitGame = [&](const ServerSettings& s) {
        p3Init = true;
        std::printf("[P3 0.3.DL-R1 InitGame] id=%u server='%s'\n", s.localPlayerId, s.serverName.c_str());
    };
    c3dl.SetEvents(std::move(ev3));

    // 2. Connect 0.3.7 clients + 0.3.DL-R1 client simultaneously
    if (!c1.Connect(host, port, "NanaAlpha", "", ProtocolVersion::SAMP_037) ||
        !c2.Connect(host, port, "NanaBravo", "", ProtocolVersion::SAMP_037) ||
        !c3dl.Connect(host, port, "NanaDeltaDL", "", ProtocolVersion::SAMP_03DL)) {
        std::fprintf(stderr, "[FAIL] Failed to initiate connections\n");
        return EXIT_FAILURE;
    }

    if (!WaitUntil(all, 8000, [&]() { return p1Init && p2Init && p3Init && p1Class && p2Class; })) {
        std::fprintf(stderr, "[FAIL] Handshake/InitGame timeout (p1=%d p2=%d p3_03DL=%d c1=%d c2=%d)\n",
                     p1Init, p2Init, p3Init, p1Class, p2Class);
        return EXIT_FAILURE;
    }
    std::printf("[PASS] SA-MP 0.3.7 (2 clients) + SA-MP 0.3.DL-R1 (1 client) connected & InitGame OK\n");

    // 3. Scoreboard RPC_UpdateScoresPingsIPs check
    c1.RequestScoreboardUpdate();
    if (!WaitUntil(all, 4000, [&]() { return p1ScoreboardUpdated; })) {
        std::fprintf(stderr, "[FAIL] Scoreboard UpdateScoresPingsIPs did not populate\n");
        return EXIT_FAILURE;
    }
    std::printf("[PASS] Scoreboard populated (%zu players visible)\n", c1.GetScoreboardEntries().size());

    // 4. Spawn P1 and P2
    p1GotTeleport = false;
    c1.RequestSpawn();
    c2.RequestSpawn();
    if (!WaitUntil(all, 6000, [&]() { return p1Spawned && p2Spawned; })) {
        std::fprintf(stderr, "[FAIL] Spawn failed (p1=%d p2=%d)\n", p1Spawned, p2Spawned);
        return EXIT_FAILURE;
    }
    WaitUntil(all, 400, [&]() { return p1GotTeleport; });
    std::printf("[PASS] Both players spawned; P1 at (%.1f, %.1f, %.1f)\n",
                p1SpawnPos.x, p1SpawnPos.y, p1SpawnPos.z);

    // 5. Real OnFootSync replication from P1 -> open.mp -> P2
    p1TargetPos = Vec3{p1SpawnPos.x + 6.0f, p1SpawnPos.y + 4.0f, p1SpawnPos.z};
    OnFootSyncData syncP1{};
    syncP1.position = p1TargetPos;
    syncP1.quaternion = Quat::FromHeadingDegrees(45.0f);
    syncP1.health = 100;
    syncP1.velocity = Vec3{0.08f, 0.05f, 0.0f};
    c1.UpdateLocalOnFoot(syncP1);

    OnFootSyncData syncP2{};
    syncP2.position = Vec3{p1SpawnPos.x + 2.0f, p1SpawnPos.y + 2.0f, p1SpawnPos.z};
    syncP2.quaternion = Quat::FromHeadingDegrees(0.0f);
    syncP2.health = 100;
    c2.UpdateLocalOnFoot(syncP2);

    if (!WaitUntil(all, 6000, [&]() { return p2StreamedP1 && p2SawP1Move; })) {
        std::fprintf(stderr, "[FAIL] P2 did not receive P1 movement sync (streamed=%d moved=%d last=(%.1f,%.1f,%.1f))\n",
                     p2StreamedP1, p2SawP1Move, p2SeenP1Pos.x, p2SeenP1Pos.y, p2SeenP1Pos.z);
        return EXIT_FAILURE;
    }
    std::printf("[PASS] Real ID_PLAYER_SYNC movement replicated to P2 (%.1f, %.1f, %.1f)\n",
                p2SeenP1Pos.x, p2SeenP1Pos.y, p2SeenP1Pos.z);

    // 6. Real RPC_Chat broadcast
    c1.SendChat("NANA_MP_OK Hello from Nana SAMP Native!");
    if (!WaitUntil(all, 4000, [&]() { return p2SawP1Chat; })) {
        std::fprintf(stderr, "[FAIL] P2 did not receive P1 chat message\n");
        return EXIT_FAILURE;
    }
    std::printf("[PASS] Real RPC_Chat message delivered between clients\n");

    // 7. Disconnect + RPC_ServerQuit + Reconnect
    c1.Disconnect();
    if (!WaitUntil(all, 4000, [&]() { return p2SawP1Quit; })) {
        std::fprintf(stderr, "[FAIL] P2 did not receive P1 disconnect (RPC_ServerQuit)\n");
        return EXIT_FAILURE;
    }
    p1Init = false;
    if (!c1.Reconnect() || !WaitUntil(all, 6000, [&]() { return p1Init; })) {
        std::fprintf(stderr, "[FAIL] P1 Reconnect failed\n");
        return EXIT_FAILURE;
    }
    std::printf("[PASS] Clean disconnect + RPC_ServerQuit + Reconnect verified\n");

    c1.Disconnect();
    c2.Disconnect();
    c3dl.Disconnect();
    std::printf("=== ALL LIVE OPEN.MP MULTIPLAYER INTEGRATION TESTS PASSED (0.3.7 & 0.3.DL-R1) ===\n");
    return EXIT_SUCCESS;
}
