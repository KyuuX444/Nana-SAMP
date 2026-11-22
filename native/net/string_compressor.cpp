#include "string_compressor.h"

#include <algorithm>
#include <list>

namespace nana::net {

namespace {

// Standard RakNet static English character frequency table (256 entries, indices 128..255 = 0).
static constexpr std::array<uint32_t, 256> kEnglishFrequencies = {{
    // 0..15
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 722, 0, 0, 2, 0, 0,
    // 16..31
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 32..47 (' ' .. '/')
    11084, 58, 63, 1, 0, 31, 0, 317, 64, 64, 44, 0, 695, 62, 980, 266,
    // 48..63 ('0' .. '?')
    69, 67, 56, 7, 73, 3, 14, 2, 69, 1, 167, 9, 1, 2, 25, 94,
    // 64..79 ('@' .. 'O')
    0, 195, 139, 34, 96, 48, 103, 56, 125, 653, 21, 5, 23, 64, 85, 44,
    // 80..95 ('P' .. '_')
    34, 7, 92, 76, 147, 12, 14, 57, 15, 39, 15, 1, 1, 1, 2, 3,
    // 96..111 ('`' .. 'o')
    0, 3611, 845, 1077, 1884, 5870, 841, 1057, 2501, 3212, 164, 531, 2019, 1330, 3056, 4037,
    // 112..127 ('p' .. DEL)
    848, 47, 2586, 2919, 4771, 1707, 535, 1106, 152, 1243, 100, 0, 2, 0, 10, 0,
    // 128..255
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
}};

} // namespace

const StringCompressor& StringCompressor::Instance() {
    static const StringCompressor instance;
    return instance;
}

StringCompressor::StringCompressor() {
    BuildTree();
}

void StringCompressor::BuildTree() {
    std::list<std::unique_ptr<Node>> sortedList;
    std::array<Node*, 256> leaves{};

    // Insert node matching RakNet's InsertNodeIntoSortedLinkedList:
    // Strictly less-than comparison so equal weights preserve insertion order (appended after equals).
    auto insertSorted = [&](std::unique_ptr<Node> node) {
        auto it = sortedList.begin();
        while (it != sortedList.end() && !(node->weight < (*it)->weight)) {
            ++it;
        }
        sortedList.insert(it, std::move(node));
    };

    for (uint32_t i = 0; i < 256; ++i) {
        auto leaf = std::make_unique<Node>();
        leaf->value = static_cast<uint8_t>(i);
        leaf->weight = (kEnglishFrequencies[i] == 0) ? 1u : kEnglishFrequencies[i];
        leaf->isLeaf = true;
        leaves[i] = leaf.get();
        insertSorted(std::move(leaf));
    }

    while (sortedList.size() > 1) {
        auto lesser = std::move(sortedList.front());
        sortedList.pop_front();
        auto greater = std::move(sortedList.front());
        sortedList.pop_front();

        auto parent = std::make_unique<Node>();
        parent->weight = lesser->weight + greater->weight;
        parent->isLeaf = false;
        lesser->parent = parent.get();
        greater->parent = parent.get();
        parent->left = std::move(lesser);
        parent->right = std::move(greater);

        insertSorted(std::move(parent));
    }

    root_ = std::move(sortedList.front());

    for (uint32_t i = 0; i < 256; ++i) {
        std::vector<uint8_t> reversed;
        Node* cur = leaves[i];
        while (cur && cur->parent) {
            reversed.push_back(cur == cur->parent->left.get() ? 0u : 1u);
            cur = cur->parent;
        }
        std::reverse(reversed.begin(), reversed.end());
        encodingTable_[i].bits = std::move(reversed);
    }
}

void StringCompressor::EncodeString(const std::string& input,
                                    size_t maxCharsToWrite,
                                    BitStream& output) const {
    if (maxCharsToWrite == 0) {
        output.WriteCompressed<uint32_t>(0);
        return;
    }

    size_t copyLen = std::min(input.size(), maxCharsToWrite - 1);
    BitStream encoded;
    for (size_t i = 0; i < copyLen; ++i) {
        uint8_t byte = static_cast<uint8_t>(input[i]);
        for (uint8_t bit : encodingTable_[byte].bits) {
            encoded.WriteBit(bit != 0);
        }
    }
    // Always include null terminator per RakNet StringCompressor spec
    for (uint8_t bit : encodingTable_[0].bits) {
        encoded.WriteBit(bit != 0);
    }

    uint32_t bitLen = static_cast<uint32_t>(encoded.GetNumberOfBitsUsed());
    output.WriteCompressed<uint32_t>(bitLen);
    if (bitLen > 0) {
        output.WriteBits(encoded.Data(), bitLen, false);
    }
}

bool StringCompressor::DecodeString(std::string& output,
                                    size_t maxCharsToWrite,
                                    BitStream& input) const {
    output.clear();
    if (maxCharsToWrite == 0) return false;

    uint32_t stringBitLength = 0;
    if (!input.ReadCompressed<uint32_t>(stringBitLength)) {
        return false;
    }
    if (stringBitLength == 0) {
        return true;
    }
    if (input.GetNumberOfUnreadBits() < stringBitLength) {
        return false;
    }

    const size_t startOffset = input.GetReadOffset();
    const size_t endOffset = startOffset + stringBitLength;

    const Node* cur = root_.get();
    while (input.GetReadOffset() < endOffset && cur != nullptr) {
        bool bit = input.ReadBit();
        cur = bit ? cur->right.get() : cur->left.get();
        if (cur && cur->isLeaf) {
            if (cur->value == 0) {
                break;
            }
            if (output.size() + 1 < maxCharsToWrite) {
                output.push_back(static_cast<char>(cur->value));
            }
            cur = root_.get();
        }
    }

    input.SetReadOffset(endOffset);
    return true;
}

} // namespace nana::net
