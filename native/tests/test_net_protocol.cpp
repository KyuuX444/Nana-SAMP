#include "../net/bitstream.h"
#include "../net/raknet_peer.h"
#include "../net/samp_client.h"
#include "../net/samp_crypto.h"
#include "../net/string_compressor.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nana::net;

namespace {

void TestBitStreamPrimitives() {
    BitStream bs;
    bs.Write<uint8_t>(0xA5);
    bs.WriteBit(true);
    bs.WriteBit(false);
    bs.WriteCompressed<uint16_t>(42);
    bs.WriteCompressed<uint32_t>(0x00123456);
    bs.WriteDynStr8("NanaPlayer");
    bs.WriteVector(1.5f, -2.25f, 0.75f);
    Quat qIn = Quat::FromHeadingDegrees(135.0f);
    bs.WriteNormQuat(qIn.w, qIn.x, qIn.y, qIn.z);

    bs.ResetReadPointer();
    uint8_t b = 0;
    assert(bs.Read<uint8_t>(b) && b == 0xA5);
    assert(bs.ReadBit() == true);
    assert(bs.ReadBit() == false);
    uint16_t c16 = 0;
    uint32_t c32 = 0;
    assert(bs.ReadCompressed<uint16_t>(c16) && c16 == 42);
    assert(bs.ReadCompressed<uint32_t>(c32) && c32 == 0x00123456);
    std::string nick;
    assert(bs.ReadDynStr8(nick) && nick == "NanaPlayer");
    float vx = 0, vy = 0, vz = 0;
    assert(bs.ReadVector(vx, vy, vz));
    assert(std::fabs(vx - 1.5f) < 0.02f && std::fabs(vy + 2.25f) < 0.02f && std::fabs(vz - 0.75f) < 0.02f);
    Quat qOut{};
    assert(bs.ReadNormQuat(qOut.w, qOut.x, qOut.y, qOut.z));
    assert(std::fabs(qOut.ToHeadingDegrees() - 135.0f) < 1.0f);
    std::printf("[PASS] BitStream primitives, compressed ints, vector & norm quaternion\n");
}

void TestRakNetStringCompressorAndShowDialog() {
    const std::string body =
        "{FFFFFF}Welcome to {00AAFF}Nana SAMP{FFFFFF}!\n"
        "Item 1\t$500\n"
        "Item 2\t$1,250\n"
        "Enter your password to login.";

    BitStream bs;
    bs.Write<int16_t>(1042);
    bs.Write<uint8_t>(DIALOG_STYLE_TABLIST_HEADERS);
    bs.WriteDynStr8("Shop Menu");
    bs.WriteDynStr8("Buy");
    bs.WriteDynStr8("Close");
    StringCompressor::Instance().EncodeString(body, 4096, bs);
    // Append sentinel after compressed string to verify bit-exact cursor alignment
    bs.Write<uint16_t>(0xBEEF);

    bs.ResetReadPointer();
    DialogData parsed{};
    assert(SampClient::ReadServerShowDialog(bs, parsed));
    assert(parsed.dialogId == 1042);
    assert(parsed.style == DIALOG_STYLE_TABLIST_HEADERS);
    assert(parsed.title == "Shop Menu");
    assert(parsed.button1 == "Buy");
    assert(parsed.button2 == "Close");
    assert(parsed.info == body);

    uint16_t sentinel = 0;
    assert(bs.Read<uint16_t>(sentinel) && sentinel == 0xBEEF);
    std::printf("[PASS] RakNet Huffman StringCompressor & RPC_ShowDialog wire codec\n");
}

void TestSampCryptoAndAuth() {
    std::vector<uint8_t> payload = {0x18, 0x01, 0x02, 0x03, 0xFE, 0xDC, 0xBA, 0x98};
    auto enc = SampCrypto::EncryptDatagram(payload.data(), payload.size(), 7777);
    assert(enc.size() == payload.size() + 1);
    auto dec = SampCrypto::DecryptDatagram(enc.data(), enc.size(), 7777);
    assert(dec == payload);

    std::string resp = SampCrypto::LookupAuthResponse("6C407EC29DE59E2");
    assert(resp == "D9412F235647BAA582089C6F66817F8B8811C057");

    std::string gpci = SampCrypto::GenerateClientSerial("NanaTest@127.0.0.1");
    assert(SampCrypto::ValidateClientSerial(gpci));
    std::printf("[PASS] SA-MP datagram encryption/decryption, auth table & GPCI serial\n");
}

void TestAckRangeList() {
    std::vector<uint16_t> ids = {1, 2, 3, 4, 10, 12, 13, 14, 100};
    BitStream bs;
    RakNetPeer::WriteAckRangeList(bs, ids);
    bs.ResetReadPointer();
    std::vector<uint16_t> decoded;
    assert(RakNetPeer::ReadAckRangeList(bs, decoded));
    assert(decoded == ids);
    std::printf("[PASS] RakNet ACK RangeList serialization\n");
}

void TestSyncCodecs() {
    OnFootSyncData of{};
    of.position = Vec3{1958.5f, 1343.2f, 15.4f};
    of.quaternion = Quat::FromHeadingDegrees(90.0f);
    of.health = 100;
    of.armour = 50;
    of.keys = SampKeys::SPRINT | SampKeys::JUMP;
    BitStream bs1;
    SampClient::WriteClientOnFootSync(bs1, of);
    assert(bs1.GetNumberOfBytesUsed() == 69); // 1 byte packetId + 68 bytes OnFootSync

    InCarSyncData ic{};
    ic.vehicleId = 12;
    ic.position = Vec3{100.0f, -200.0f, 13.5f};
    ic.carHealth = 980.0f;
    BitStream bs2;
    SampClient::WriteClientInCarSync(bs2, ic);
    assert(bs2.GetNumberOfBytesUsed() == 64); // 1 byte packetId + 63 bytes InCarSync

    PassengerSyncData ps{};
    ps.vehicleId = 12;
    ps.seatId = 2;
    ps.driveBy = true;
    ps.health = 85;
    ps.position = Vec3{100.0f, -200.0f, 13.5f};
    BitStream bs3;
    SampClient::WriteClientPassengerSync(bs3, ps);
    assert(bs3.GetNumberOfBytesUsed() == 25); // 1 byte packetId + 24 bytes PassengerSync

    // Test server->client passenger sync parse
    BitStream bsSrvPass;
    bsSrvPass.Write<uint8_t>(ID_PASSENGER_SYNC);
    bsSrvPass.Write<uint16_t>(7);
    bsSrvPass.Write<uint16_t>(12);
    bsSrvPass.Write<uint8_t>(static_cast<uint8_t>(2 | 0x40));
    bsSrvPass.Write<uint8_t>(24);
    bsSrvPass.Write<uint8_t>(85);
    bsSrvPass.Write<uint8_t>(40);
    bsSrvPass.Write<uint16_t>(0);
    bsSrvPass.Write<uint16_t>(0);
    bsSrvPass.Write<uint16_t>(SampKeys::FIRE);
    bsSrvPass.Write<float>(100.0f);
    bsSrvPass.Write<float>(-200.0f);
    bsSrvPass.Write<float>(13.5f);
    bsSrvPass.ResetReadPointer();
    uint16_t outPid = 0;
    PassengerSyncData outPs{};
    assert(SampClient::ReadServerPassengerSync(bsSrvPass, outPid, outPs));
    assert(outPid == 7 && outPs.vehicleId == 12 && outPs.seatId == 2 && outPs.driveBy && outPs.health == 85);

    AimSyncData aim{};
    aim.camFront = Vec3{0.0f, 1.0f, 0.0f};
    aim.camPos = Vec3{10.0f, 20.0f, 15.0f};
    BitStream bs4;
    SampClient::WriteClientAimSync(bs4, aim);
    assert(bs4.GetNumberOfBytesUsed() == 32); // 1 byte packetId + 31 bytes AimSync

    assert(static_cast<uint32_t>(ProtocolVersion::SAMP_037) == 4057);
    assert(static_cast<uint32_t>(ProtocolVersion::SAMP_03DL) == 4062);
    std::printf("[PASS] OnFoot / InCar / Passenger / Aim sync codecs & protocol versions (0.3.7 & 0.3.DL-R1)\n");
}

void TestTextDrawWireCodec() {
    BitStream bs;
    uint16_t tdId = 2048 + 12; // Player TextDraw #12
    uint8_t flags = 0x01u | (4u << 1) | 0x10u; // useBox + Center + proportional
    bs.Write<uint16_t>(tdId);
    bs.Write<uint8_t>(flags);
    bs.Write<float>(0.45f);
    bs.Write<float>(1.20f);
    bs.Write<uint32_t>(0xFF00CCFFu); // ABGR
    bs.Write<float>(180.0f);
    bs.Write<float>(32.0f);
    bs.Write<uint32_t>(0xAA101010u);
    bs.Write<uint8_t>(1);
    bs.Write<uint8_t>(1);
    bs.Write<uint32_t>(0xFF000000u);
    bs.Write<uint8_t>(2); // font 2
    bs.Write<uint8_t>(1); // selectable
    bs.Write<float>(320.0f);
    bs.Write<float>(210.0f);
    bs.Write<uint16_t>(0);
    bs.Write<float>(0.0f); bs.Write<float>(0.0f); bs.Write<float>(0.0f);
    bs.Write<float>(1.0f);
    bs.Write<int16_t>(-1); bs.Write<int16_t>(-1);
    bs.WriteDynStr16("~g~LOGIN ~w~BUTTON");

    bs.ResetReadPointer();
    TextDrawData parsed{};
    assert(SampClient::ReadServerShowTextDraw(bs, parsed));
    assert(parsed.id == 2060);
    assert(parsed.useBox && parsed.alignment == 2 && parsed.proportional && parsed.selectable);
    assert(std::fabs(parsed.x - 320.0f) < 0.01f && std::fabs(parsed.y - 210.0f) < 0.01f);
    assert(parsed.text == "~g~LOGIN ~w~BUTTON");
    std::printf("[PASS] RPC_ShowTextDraw (134) global/player TextDraw wire codec\n");
}

} // namespace

int main() {
    TestBitStreamPrimitives();
    TestRakNetStringCompressorAndShowDialog();
    TestSampCryptoAndAuth();
    TestAckRangeList();
    TestSyncCodecs();
    TestTextDrawWireCodec();
    std::printf("=== ALL NET PROTOCOL UNIT TESTS PASSED ===\n");
    return EXIT_SUCCESS;
}
