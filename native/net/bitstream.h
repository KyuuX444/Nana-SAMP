#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <type_traits>

namespace nana::net {

/**
 * RakNet 2.x / SA-MP 0.3.7 wire-compatible BitStream.
 *
 * Bit order matches RakNet Legacy:
 * - Single bits and sub-byte fields are packed MSB-first within each byte
 *   (bit 0 of the stream is 0x80 of byte 0).
 * - Multi-byte integers/floats are little-endian (native on x86_64 and ARM64).
 */
class BitStream {
public:
    BitStream();
    explicit BitStream(size_t initialBytes);
    BitStream(const uint8_t* data, size_t byteLength);
    BitStream(const uint8_t* data, size_t bitLength, bool isBitLength);

    void Reset();
    void ResetReadPointer() { readOffset_ = 0; }
    void ResetWritePointer() { writeOffset_ = 0; buffer_.clear(); }

    const uint8_t* Data() const { return buffer_.data(); }
    uint8_t* Data() { return buffer_.data(); }

    size_t GetNumberOfBitsUsed() const { return writeOffset_; }
    size_t GetNumberOfBytesUsed() const { return (writeOffset_ + 7) >> 3; }
    size_t GetReadOffset() const { return readOffset_; }
    size_t GetNumberOfUnreadBits() const {
        return readOffset_ >= writeOffset_ ? 0 : (writeOffset_ - readOffset_);
    }

    void SetWriteOffset(size_t bitOffset);
    void SetReadOffset(size_t bitOffset) { readOffset_ = bitOffset; }
    void IgnoreBits(size_t bits) { readOffset_ += bits; }
    void IgnoreBytes(size_t bytes) { readOffset_ += bytes * 8; }

    void AlignWriteToByteBoundary();
    void AlignReadToByteBoundary();

    void Write0();
    void Write1();
    void WriteBit(bool value) { if (value) Write1(); else Write0(); }

    void WriteBits(const uint8_t* input, size_t numBits, bool rightAlignedBits = true);
    void WriteBytes(const void* data, size_t numBytes);
    void WriteAlignedBytes(const uint8_t* data, size_t numBytes);

    bool ReadBit();
    bool ReadBits(uint8_t* output, size_t numBits, bool alignBitsToRight = true);
    bool ReadBytes(void* output, size_t numBytes);
    bool ReadAlignedBytes(uint8_t* output, size_t numBytes);

    // Typed Write / Read
    template <typename T>
    void Write(const T& val) {
        static_assert(std::is_trivially_copyable_v<T>, "BitStream::Write requires trivially copyable type");
        if constexpr (std::is_same_v<T, bool>) {
            WriteBit(val);
        } else {
            WriteBits(reinterpret_cast<const uint8_t*>(&val), sizeof(T) * 8, true);
        }
    }

    template <typename T>
    bool Read(T& out) {
        static_assert(std::is_trivially_copyable_v<T>, "BitStream::Read requires trivially copyable type");
        if constexpr (std::is_same_v<T, bool>) {
            if (GetNumberOfUnreadBits() < 1) return false;
            out = ReadBit();
            return true;
        } else {
            return ReadBits(reinterpret_cast<uint8_t*>(&out), sizeof(T) * 8, true);
        }
    }

    // RakNet WriteCompressed / ReadCompressed (unsigned & signed integral types + float in [-1, 1])
    void WriteCompressedRaw(const uint8_t* input, size_t bitSize, bool unsignedData);
    bool ReadCompressedRaw(uint8_t* output, size_t bitSize, bool unsignedData);

    template <typename T>
    void WriteCompressed(T val) {
        if constexpr (std::is_same_v<T, bool>) {
            WriteBit(val);
        } else if constexpr (std::is_same_v<T, float>) {
            if (val < -1.0f) val = -1.0f;
            if (val > 1.0f) val = 1.0f;
            uint16_t compressed = static_cast<uint16_t>((val + 1.0f) * 32767.5f);
            Write<uint16_t>(compressed);
        } else {
            static_assert(std::is_integral_v<T>, "WriteCompressed supports integral, bool, or float");
            WriteCompressedRaw(reinterpret_cast<const uint8_t*>(&val), sizeof(T) * 8, std::is_unsigned_v<T>);
        }
    }

    template <typename T>
    bool ReadCompressed(T& out) {
        if constexpr (std::is_same_v<T, bool>) {
            return Read<bool>(out);
        } else if constexpr (std::is_same_v<T, float>) {
            uint16_t compressed = 0;
            if (!Read<uint16_t>(compressed)) return false;
            out = (static_cast<float>(compressed) / 32767.5f) - 1.0f;
            return true;
        } else {
            static_assert(std::is_integral_v<T>, "ReadCompressed supports integral, bool, or float");
            return ReadCompressedRaw(reinterpret_cast<uint8_t*>(&out), sizeof(T) * 8, std::is_unsigned_v<T>);
        }
    }

    // RakNet compressed vector & normalized quaternion (used by SA-MP server->client sync packets)
    void WriteVector(float x, float y, float z);
    bool ReadVector(float& x, float& y, float& z);

    void WriteNormQuat(float w, float x, float y, float z);
    bool ReadNormQuat(float& w, float& x, float& y, float& z);

    // SA-MP length-prefixed strings
    void WriteDynStr8(const std::string& str);
    void WriteDynStr16(const std::string& str);
    void WriteDynStr32(const std::string& str);

    bool ReadDynStr8(std::string& out);
    bool ReadDynStr16(std::string& out);
    bool ReadDynStr32(std::string& out);

    void WriteBitStream(const BitStream& other);

private:
    void EnsureCapacityBits(size_t extraBits);

    std::vector<uint8_t> buffer_;
    size_t writeOffset_ = 0;
    size_t readOffset_ = 0;
};

} // namespace nana::net
