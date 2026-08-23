// Reverie/Runtime/Serialization/Hdsrf.cpp - see Hdsrf.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Serialization/Hdsrf.h"

#include "Serialization/BinaryStream.h"

namespace reverie {

namespace {
inline i16 FloatToI16(f32 v) {
    if (v > 1.0f) v = 1.0f;
    else if (v < -1.0f) v = -1.0f;
    // Symmetric-ish: scale by 32767 and round to nearest.
    const f32 s = v * 32767.0f;
    return static_cast<i16>(s >= 0.0f ? s + 0.5f : s - 0.5f);
}
inline f32 I16ToFloat(i16 v) { return static_cast<f32>(v) * (1.0f / 32768.0f); }

// Fixed header size (before the seek table): magic,version,sampleRate,channels (4×u32=16) +
// totalFrames (u64=8) + chunkFrames,chunkCount,format (3×u32=12) = 36 bytes.
constexpr usize kHeaderBytes = 36;
} // namespace

Result CookHdsrf(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate,
                 u32 chunkFrames, std::vector<u8>& out) {
    if (interleaved == nullptr || channels == 0 || sampleRate == 0) return Result::InvalidArgument;
    if (chunkFrames == 0) chunkFrames = kHdsrfDefaultChunkFrames;
    const u32 chunkCount =
        frameCount == 0 ? 0 : ((frameCount + chunkFrames - 1) / chunkFrames);

    BinaryWriter w(out);
    w.U32(kHdsrfMagic);
    w.U32(kHdsrfVersion);
    w.U32(sampleRate);
    w.U32(channels);
    w.U64(static_cast<u64>(frameCount));
    w.U32(chunkFrames);
    w.U32(chunkCount);
    w.U32(static_cast<u32>(HdsrfFormat::Pcm16));

    // Seek table: byte offset of each chunk from the start of the file. For Pcm16 the sizes are
    // deterministic, but we still write the table so streaming code (and future variable-size
    // codecs) can seek without rescanning.
    const u64 dataStart = static_cast<u64>(kHeaderBytes) + static_cast<u64>(chunkCount) * 8ull;
    u64 offset = dataStart;
    for (u32 i = 0; i < chunkCount; ++i) {
        w.U64(offset);
        const u32 framesInChunk =
            (i + 1 == chunkCount) ? (frameCount - i * chunkFrames) : chunkFrames;
        offset += static_cast<u64>(framesInChunk) * channels * sizeof(i16);
    }

    // Chunk payload: interleaved i16.
    for (u32 i = 0; i < chunkCount; ++i) {
        const u32 first = i * chunkFrames;
        const u32 framesInChunk = (i + 1 == chunkCount) ? (frameCount - first) : chunkFrames;
        for (u32 f = 0; f < framesInChunk; ++f)
            for (u32 c = 0; c < channels; ++c) {
                const i16 s = FloatToI16(interleaved[static_cast<usize>(first + f) * channels + c]);
                w.U8(static_cast<u8>(s & 0xFF));
                w.U8(static_cast<u8>((static_cast<u16>(s) >> 8) & 0xFF));
            }
    }
    return Result::Ok;
}

Result ReadHdsrfHeader(const u8* data, usize size, HdsrfHeader& out) {
    if (data == nullptr) return Result::InvalidArgument;
    BinaryReader r(data, size);
    const u32 magic = r.U32();
    if (!r.Ok() || magic != kHdsrfMagic) return Result::Unsupported;
    const u32 version = r.U32();
    if (!r.Ok() || version == 0 || version > kHdsrfVersion) return Result::Unsupported;

    out = HdsrfHeader{};
    out.version = version;
    out.sampleRate = r.U32();
    out.channels = r.U32();
    out.totalFrames = r.U64();
    out.chunkFrames = r.U32();
    out.chunkCount = r.U32();
    const u32 fmt = r.U32();
    if (!r.Ok()) return Result::Error;
    if (fmt != static_cast<u32>(HdsrfFormat::Pcm16)) return Result::Unsupported;
    if (out.channels == 0 || out.sampleRate == 0) return Result::Error;
    out.format = HdsrfFormat::Pcm16;

    // Seek table must fit and each offset must be within the buffer.
    if (static_cast<u64>(out.chunkCount) * 8ull > r.Remaining()) return Result::Error;
    out.chunkOffsets.reserve(out.chunkCount);
    for (u32 i = 0; i < out.chunkCount; ++i) {
        const u64 off = r.U64();
        if (!r.Ok() || off > size) return Result::Error;
        out.chunkOffsets.push_back(off);
    }
    return Result::Ok;
}

Result DecodeHdsrfChunk(const u8* data, usize size, const HdsrfHeader& header, u32 chunkIndex,
                        f32* out, u32 outCapFrames, u32* framesOut) {
    if (framesOut != nullptr) *framesOut = 0;
    if (data == nullptr || out == nullptr) return Result::InvalidArgument;
    if (chunkIndex >= header.chunkCount || chunkIndex >= header.chunkOffsets.size())
        return Result::NotFound;

    const u32 first = chunkIndex * header.chunkFrames;
    if (first >= header.totalFrames) return Result::Error;
    const u32 framesInChunk = (chunkIndex + 1 == header.chunkCount)
                                  ? static_cast<u32>(header.totalFrames - first)
                                  : header.chunkFrames;
    if (framesInChunk > outCapFrames) return Result::InvalidArgument;

    const u64 off = header.chunkOffsets[chunkIndex];
    const u64 need = static_cast<u64>(framesInChunk) * header.channels * sizeof(i16);
    if (off + need > size) return Result::Error;

    const u8* p = data + off;
    const usize n = static_cast<usize>(framesInChunk) * header.channels;
    for (usize i = 0; i < n; ++i) {
        const i16 s = static_cast<i16>(static_cast<u16>(p[i * 2]) |
                                       (static_cast<u16>(p[i * 2 + 1]) << 8));
        out[i] = I16ToFloat(s);
    }
    if (framesOut != nullptr) *framesOut = framesInChunk;
    return Result::Ok;
}

Result HdsrfStream::Open(const u8* data, usize size) {
    const Result r = ReadHdsrfHeader(data, size, header_);
    if (Failed(r)) return r;
    data_ = data;
    size_ = size;
    pos_ = 0;
    loadedChunk_ = 0xFFFFFFFFu;
    loadedFrames_ = 0;
    chunkBuf_.assign(static_cast<usize>(header_.chunkFrames) * header_.channels, 0.0f); // ONE chunk
    return Result::Ok;
}

void HdsrfStream::Seek(u64 frame) {
    pos_ = frame > header_.totalFrames ? header_.totalFrames : frame;
}

u32 HdsrfStream::Read(f32* out, u32 frames, bool loop) {
    if (data_ == nullptr || out == nullptr || header_.channels == 0 || header_.chunkFrames == 0)
        return 0;
    const u32 ch = header_.channels;
    u32 written = 0;
    while (written < frames) {
        if (pos_ >= header_.totalFrames) {
            if (!loop || header_.totalFrames == 0) break;
            pos_ = 0; // wrap
        }
        const u32 chunk = static_cast<u32>(pos_ / header_.chunkFrames);
        if (chunk != loadedChunk_) {
            u32 got = 0;
            if (Failed(DecodeHdsrfChunk(data_, size_, header_, chunk, chunkBuf_.data(),
                                        header_.chunkFrames, &got)))
                break;
            loadedChunk_ = chunk;
            loadedFrames_ = got;
        }
        const u32 within = static_cast<u32>(pos_ - static_cast<u64>(chunk) * header_.chunkFrames);
        if (within >= loadedFrames_) break; // corrupt / short chunk
        for (u32 c = 0; c < ch; ++c)
            out[static_cast<usize>(written) * ch + c] = chunkBuf_[static_cast<usize>(within) * ch + c];
        ++pos_;
        ++written;
    }
    return written;
}

Result ReadHdsrf(const u8* data, usize size, AudioBuffer& out) {
    HdsrfHeader h;
    const Result r = ReadHdsrfHeader(data, size, h);
    if (Failed(r)) return r;

    out.channels = h.channels;
    out.sampleRate = h.sampleRate;
    out.samples.assign(static_cast<usize>(h.totalFrames) * h.channels, 0.0f);
    if (h.totalFrames == 0) return Result::Ok;

    for (u32 i = 0; i < h.chunkCount; ++i) {
        const u32 first = i * h.chunkFrames;
        u32 written = 0;
        const Result cr = DecodeHdsrfChunk(data, size, h, i,
                                           out.samples.data() + static_cast<usize>(first) * h.channels,
                                           h.chunkFrames, &written);
        if (Failed(cr)) return cr;
    }
    return Result::Ok;
}

} // namespace reverie
