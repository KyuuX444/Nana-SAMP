#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace nana::net {

/**
 * SA-MP 0.3.7 UDP datagram obfuscation (client -> server) and authentication helpers.
 *
 * Outgoing client datagrams prepend a 1-byte checksum (XOR of (plainByte & 0xAA)),
 * substitute each byte via the inverse of the SA-MP 256-byte table, and XOR even-indexed
 * wire positions (i = 2, 4, ...) with (serverPort ^ 0xCC).
 */
class SampCrypto {
public:
    // Encrypts a client->server RakNet datagram. Returns encrypted bytes (len + 1).
    static std::vector<uint8_t> EncryptDatagram(const uint8_t* data, size_t len, uint16_t serverPort);

    // Decrypts a client->server datagram (used for self-tests and loopback verification).
    // Returns empty vector if checksum fails or len < 2.
    static std::vector<uint8_t> DecryptDatagram(const uint8_t* data, size_t len, uint16_t serverPort);

    // Looks up the official SA-MP 0.3.7 ID_AUTH_KEY response for a server challenge string.
    // Returns empty string if challenge is unrecognized.
    static std::string LookupAuthResponse(const std::string& serverChallenge);

    // Generates a protocol-valid GPCI serial (hex integer divisible by 1001, 1 < quotientLen < 50).
    static std::string GenerateClientSerial(const std::string& seed);

    // Validates that a GPCI serial satisfies the open.mp / SA-MP divisibility-by-1001 check.
    static bool ValidateClientSerial(const std::string& hexSerial);

    static size_t GetAuthTableSize();
};

} // namespace nana::net
