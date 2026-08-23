// Reverie/Runtime/Serialization/BinaryStream.h - little-endian binary read/write helpers.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The primitive under every Reverie binary format (banks, and later the .HDSRF audio container).
// Writer appends to a byte vector; Reader consumes with BOUNDS CHECKS - a truncated/corrupt buffer
// makes Reader::Ok() go false rather than reading out of bounds. Everything is stored little-endian
// with fixed-width types so a file written on one platform reads on another.
#pragma once

#include "Core/Types.h"

#include <cstring>
#include <string>
#include <vector>

namespace reverie {

class BinaryWriter {
public:
    explicit BinaryWriter(std::vector<u8>& out) : out_(out) {}

    void U8(u8 v) { out_.push_back(v); }
    void U32(u32 v) {
        for (int i = 0; i < 4; ++i) out_.push_back(static_cast<u8>((v >> (i * 8)) & 0xFF));
    }
    void U64(u64 v) {
        for (int i = 0; i < 8; ++i) out_.push_back(static_cast<u8>((v >> (i * 8)) & 0xFF));
    }
    void I32(i32 v) { U32(static_cast<u32>(v)); }
    void F32(f32 v) {
        u32 bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }
    // Length-prefixed (u32) UTF-8 string.
    void Str(const std::string& s) {
        U32(static_cast<u32>(s.size()));
        out_.insert(out_.end(), s.begin(), s.end());
    }
    void Bytes(const void* p, usize n) {
        const u8* b = static_cast<const u8*>(p);
        out_.insert(out_.end(), b, b + n);
    }

private:
    std::vector<u8>& out_;
};

class BinaryReader {
public:
    BinaryReader(const u8* data, usize size) : data_(data), size_(size) {}

    bool Ok() const { return ok_; }
    usize Remaining() const { return ok_ ? size_ - pos_ : 0; }

    u8 U8() {
        if (!Need(1)) return 0;
        return data_[pos_++];
    }
    u32 U32() {
        if (!Need(4)) return 0;
        u32 v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<u32>(data_[pos_++]) << (i * 8);
        return v;
    }
    u64 U64() {
        if (!Need(8)) return 0;
        u64 v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<u64>(data_[pos_++]) << (i * 8);
        return v;
    }
    i32 I32() { return static_cast<i32>(U32()); }
    f32 F32() {
        const u32 bits = U32();
        f32 v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }
    std::string Str() {
        const u32 n = U32();
        if (!Need(n)) return std::string();
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return s;
    }

private:
    bool Need(usize n) {
        if (!ok_ || pos_ + n > size_) {
            ok_ = false;
            return false;
        }
        return true;
    }

    const u8* data_;
    usize size_;
    usize pos_ = 0;
    bool ok_ = true;
};

} // namespace reverie
