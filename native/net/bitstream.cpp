#include "bitstream.h"
#include <algorithm>
#include <cmath>

namespace nana::net {

BitStream::BitStream() {
    buffer_.reserve(256);
}

BitStream::BitStream(size_t initialBytes) {
    buffer_.reserve(initialBytes);
}

BitStream::BitStream(const uint8_t* data, size_t byteLength)
    : buffer_(data, data + byteLength),
      writeOffset_(byteLength * 8),
      readOffset_(0) {}

BitStream::BitStream(const uint8_t* data, size_t bitLength, bool isBitLength)
    : readOffset_(0) {
    if (isBitLength) {
        size_t bytes = (bitLength + 7) >> 3;
        buffer_.assign(data, data + bytes);
        writeOffset_ = bitLength;
    } else {
        buffer_.assign(data, data + bitLength);
        writeOffset_ = bitLength * 8;
    }
}

void BitStream::Reset() {
    buffer_.clear();
    writeOffset_ = 0;
    readOffset_ = 0;
}

void BitStream::SetWriteOffset(size_t bitOffset) {
    size_t neededBytes = (bitOffset + 7) >> 3;
    if (neededBytes > buffer_.size()) {
        buffer_.resize(neededBytes, 0);
    }
    writeOffset_ = bitOffset;
}

void BitStream::EnsureCapacityBits(size_t extraBits) {
    size_t totalBits = writeOffset_ + extraBits;
    size_t neededBytes = (totalBits + 7) >> 3;
    if (neededBytes > buffer_.size()) {
        buffer_.resize(neededBytes, 0);
    }
}

void BitStream::AlignWriteToByteBoundary() {
    if ((writeOffset_ & 7u) != 0) {
        size_t pad = 8u - (writeOffset_ & 7u);
        EnsureCapacityBits(pad);
        writeOffset_ += pad;
    }
}

void BitStream::AlignReadToByteBoundary() {
    if ((readOffset_ & 7u) != 0) {
        readOffset_ += 8u - (readOffset_ & 7u);
    }
}

void BitStream::Write0() {
    EnsureCapacityBits(1);
    if ((writeOffset_ & 7u) == 0) {
        buffer_[writeOffset_ >> 3] = 0;
    }
    writeOffset_++;
}

void BitStream::Write1() {
    EnsureCapacityBits(1);
    size_t mod8 = writeOffset_ & 7u;
    if (mod8 == 0) {
        buffer_[writeOffset_ >> 3] = 0x80;
    } else {
        buffer_[writeOffset_ >> 3] |= static_cast<uint8_t>(0x80u >> mod8);
    }
    writeOffset_++;
}

void BitStream::WriteBits(const uint8_t* input, size_t numBits, bool rightAlignedBits) {
    if (numBits == 0 || input == nullptr) return;
    EnsureCapacityBits(numBits);

    size_t offset = 0;
    size_t mod8 = writeOffset_ & 7u;
    size_t remaining = numBits;

    while (remaining > 0) {
        uint8_t dataByte = input[offset];
        if (remaining < 8 && rightAlignedBits) {
            dataByte = static_cast<uint8_t>(dataByte << (8 - remaining));
        }

        size_t byteIdx = writeOffset_ >> 3;
        if (mod8 == 0) {
            buffer_[byteIdx] = dataByte;
        } else {
            buffer_[byteIdx] |= static_cast<uint8_t>(dataByte >> mod8);
            if ((8 - mod8) < remaining) {
                buffer_[byteIdx + 1] = static_cast<uint8_t>(dataByte << (8 - mod8));
            }
        }

        if (remaining >= 8) {
            writeOffset_ += 8;
            remaining -= 8;
        } else {
            writeOffset_ += remaining;
            remaining = 0;
        }
        offset++;
    }
}

void BitStream::WriteBytes(const void* data, size_t numBytes) {
    if (numBytes == 0 || data == nullptr) return;
    WriteBits(static_cast<const uint8_t*>(data), numBytes * 8, true);
}

void BitStream::WriteAlignedBytes(const uint8_t* data, size_t numBytes) {
    if (numBytes == 0 || data == nullptr) return;
    AlignWriteToByteBoundary();
    WriteBytes(data, numBytes);
}

bool BitStream::ReadBit() {
    if (GetNumberOfUnreadBits() < 1) return false;
    bool res = (buffer_[readOffset_ >> 3] & (0x80u >> (readOffset_ & 7u))) != 0;
    readOffset_++;
    return res;
}

bool BitStream::ReadBits(uint8_t* output, size_t numBits, bool alignBitsToRight) {
    if (numBits == 0) return true;
    if (output == nullptr || GetNumberOfUnreadBits() < numBits) return false;

    size_t outBytes = (numBits + 7) >> 3;
    std::memset(output, 0, outBytes);

    size_t mod8 = readOffset_ & 7u;
    size_t offset = 0;
    size_t remaining = numBits;

    while (remaining > 0) {
        size_t byteIdx = readOffset_ >> 3;
        output[offset] |= static_cast<uint8_t>(buffer_[byteIdx] << mod8);
        if (mod8 > 0 && remaining > (8 - mod8)) {
            output[offset] |= static_cast<uint8_t>(buffer_[byteIdx + 1] >> (8 - mod8));
        }

        if (remaining < 8) {
            if (alignBitsToRight) {
                output[offset] >>= (8 - remaining);
            }
            readOffset_ += remaining;
            remaining = 0;
        } else {
            readOffset_ += 8;
            remaining -= 8;
        }
        offset++;
    }
    return true;
}

bool BitStream::ReadBytes(void* output, size_t numBytes) {
    if (numBytes == 0) return true;
    return ReadBits(static_cast<uint8_t*>(output), numBytes * 8, true);
}

bool BitStream::ReadAlignedBytes(uint8_t* output, size_t numBytes) {
    if (numBytes == 0) return true;
    AlignReadToByteBoundary();
    if (GetNumberOfUnreadBits() < numBytes * 8) return false;
    std::memcpy(output, buffer_.data() + (readOffset_ >> 3), numBytes);
    readOffset_ += numBytes * 8;
    return true;
}

void BitStream::WriteCompressedRaw(const uint8_t* input, size_t bitSize, bool unsignedData) {
    int currentByte = static_cast<int>(bitSize >> 3) - 1;
    uint8_t byteMatch = unsignedData ? 0x00 : 0xFF;

    while (currentByte > 0) {
        if (input[currentByte] == byteMatch) {
            Write1();
        } else {
            Write0();
            WriteBits(input, static_cast<size_t>(currentByte + 1) * 8, true);
            return;
        }
        currentByte--;
    }

    uint8_t upperHalf = input[currentByte] & 0xF0;
    if ((unsignedData && upperHalf == 0x00) || (!unsignedData && upperHalf == 0xF0)) {
        Write1();
        WriteBits(input + currentByte, 4, true);
    } else {
        Write0();
        WriteBits(input + currentByte, 8, true);
    }
}

bool BitStream::ReadCompressedRaw(uint8_t* output, size_t bitSize, bool unsignedData) {
    int currentByte = static_cast<int>(bitSize >> 3) - 1;
    uint8_t byteMatch = unsignedData ? 0x00 : 0xFF;
    uint8_t halfByteMatch = unsignedData ? 0x00 : 0xF0;

    while (currentByte > 0) {
        bool isMatch = false;
        if (!Read<bool>(isMatch)) return false;
        if (isMatch) {
            output[currentByte] = byteMatch;
            currentByte--;
        } else {
            return ReadBits(output, static_cast<size_t>(currentByte + 1) * 8, true);
        }
    }

    bool isHalfMatch = false;
    if (!Read<bool>(isHalfMatch)) return false;
    if (isHalfMatch) {
        if (!ReadBits(output + currentByte, 4, true)) return false;
        output[currentByte] |= halfByteMatch;
    } else {
        if (!ReadBits(output + currentByte, 8, true)) return false;
    }
    return true;
}

void BitStream::WriteVector(float x, float y, float z) {
    float mag = std::sqrt(x * x + y * y + z * z);
    Write<float>(mag);
    if (mag > 0.00001f) {
        WriteCompressed<float>(x / mag);
        WriteCompressed<float>(y / mag);
        WriteCompressed<float>(z / mag);
    }
}

bool BitStream::ReadVector(float& x, float& y, float& z) {
    float mag = 0.0f;
    if (!Read<float>(mag)) return false;
    if (mag != 0.0f) {
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        if (!ReadCompressed<float>(cx)) return false;
        if (!ReadCompressed<float>(cy)) return false;
        if (!ReadCompressed<float>(cz)) return false;
        x = cx * mag;
        y = cy * mag;
        z = cz * mag;
    } else {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
    }
    return true;
}

void BitStream::WriteNormQuat(float w, float x, float y, float z) {
    Write<bool>(w < 0.0f);
    Write<bool>(x < 0.0f);
    Write<bool>(y < 0.0f);
    Write<bool>(z < 0.0f);
    Write<uint16_t>(static_cast<uint16_t>(std::min(1.0f, std::fabs(x)) * 65535.0f));
    Write<uint16_t>(static_cast<uint16_t>(std::min(1.0f, std::fabs(y)) * 65535.0f));
    Write<uint16_t>(static_cast<uint16_t>(std::min(1.0f, std::fabs(z)) * 65535.0f));
}

bool BitStream::ReadNormQuat(float& w, float& x, float& y, float& z) {
    bool cwNeg = false, cxNeg = false, cyNeg = false, czNeg = false;
    uint16_t cx = 0, cy = 0, cz = 0;
    if (!Read<bool>(cwNeg)) return false;
    if (!Read<bool>(cxNeg)) return false;
    if (!Read<bool>(cyNeg)) return false;
    if (!Read<bool>(czNeg)) return false;
    if (!Read<uint16_t>(cx)) return false;
    if (!Read<uint16_t>(cy)) return false;
    if (!Read<uint16_t>(cz)) return false;

    x = static_cast<float>(cx) / 65535.0f;
    y = static_cast<float>(cy) / 65535.0f;
    z = static_cast<float>(cz) / 65535.0f;
    if (cxNeg) x = -x;
    if (cyNeg) y = -y;
    if (czNeg) z = -z;

    float diff = 1.0f - x * x - y * y - z * z;
    if (diff < 0.0f) diff = 0.0f;
    w = std::sqrt(diff);
    if (cwNeg) w = -w;
    return true;
}

void BitStream::WriteDynStr8(const std::string& str) {
    uint8_t len = static_cast<uint8_t>(std::min<size_t>(str.size(), 255));
    Write<uint8_t>(len);
    if (len > 0) WriteBytes(str.data(), len);
}

void BitStream::WriteDynStr16(const std::string& str) {
    uint16_t len = static_cast<uint16_t>(std::min<size_t>(str.size(), 65535));
    Write<uint16_t>(len);
    if (len > 0) WriteBytes(str.data(), len);
}

void BitStream::WriteDynStr32(const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    Write<uint32_t>(len);
    if (len > 0) WriteBytes(str.data(), len);
}

bool BitStream::ReadDynStr8(std::string& out) {
    uint8_t len = 0;
    if (!Read<uint8_t>(len)) return false;
    if (GetNumberOfUnreadBits() < static_cast<size_t>(len) * 8) return false;
    out.resize(len);
    if (len > 0 && !ReadBytes(out.data(), len)) return false;
    return true;
}

bool BitStream::ReadDynStr16(std::string& out) {
    uint16_t len = 0;
    if (!Read<uint16_t>(len)) return false;
    if (GetNumberOfUnreadBits() < static_cast<size_t>(len) * 8) return false;
    out.resize(len);
    if (len > 0 && !ReadBytes(out.data(), len)) return false;
    return true;
}

bool BitStream::ReadDynStr32(std::string& out) {
    uint32_t len = 0;
    if (!Read<uint32_t>(len)) return false;
    if (len > 65536 || GetNumberOfUnreadBits() < static_cast<size_t>(len) * 8) return false;
    out.resize(len);
    if (len > 0 && !ReadBytes(out.data(), len)) return false;
    return true;
}

void BitStream::WriteBitStream(const BitStream& other) {
    if (other.GetNumberOfBitsUsed() > 0) {
        WriteBits(other.Data(), other.GetNumberOfBitsUsed(), false);
    }
}

} // namespace nana::net
