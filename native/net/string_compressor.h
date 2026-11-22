#pragma once

#include "bitstream.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nana::net {

/**
 * Clean-room RakNet Huffman StringCompressor compatible with SA-MP 0.3.7 / 0.3.DL / open.mp.
 *
 * Uses the exact 256-entry English character frequency table verified against SA-MP wire packets
 * (used by RPC_ShowDialog, RPC_Create3DTextLabel, and other compressed SA-MP strings).
 */
class StringCompressor {
public:
    static const StringCompressor& Instance();

    void EncodeString(const std::string& input, size_t maxCharsToWrite, BitStream& output) const;
    bool DecodeString(std::string& output, size_t maxCharsToWrite, BitStream& input) const;

private:
    struct Node {
        uint8_t value = 0;
        uint32_t weight = 0;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        Node* parent = nullptr;
        bool isLeaf = false;
    };

    struct SymbolCode {
        std::vector<uint8_t> bits; // 0 or 1, MSB (root->leaf) order
    };

    StringCompressor();
    void BuildTree();

    std::unique_ptr<Node> root_;
    std::array<SymbolCode, 256> encodingTable_{};
};

} // namespace nana::net
