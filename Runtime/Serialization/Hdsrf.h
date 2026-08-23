// Reverie/Runtime/Serialization/Hdsrf.h - the .HDSRF cooked runtime audio container.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// .HDSRF (Hollow Dream Studios Reverie File) is Reverie's cooked runtime audio format: a versioned
// container around a proven payload encoding, NOT a new codec. v1 stores 16-bit PCM (halving f32
// size with no audible loss for game audio) split into fixed-frame CHUNKS with a byte-offset SEEK
// TABLE - the structure real streaming needs (decode/prefetch one chunk without touching the rest).
// The header carries a magic + version + format tag so a corrupt/newer/foreign file is REJECTED,
// never misread. A future version can swap the chunk payload for a compressed codec behind the same
// container + seek table; the seek table is why chunks may become variable-size.
//
// This is a pure codec (no device, no threading): the cooker turns source PCM into a blob; the
// runtime decodes a blob (or one chunk) back to f32. Streaming voices (a background reader feeding
// the audio thread) build on DecodeChunk in a later phase.
#pragma once

#include "Audio/AudioBuffer.h"
#include "Core/Types.h"

#include <vector>

namespace reverie {

constexpr u32 kHdsrfMagic = 0x46524448u; // 'HDRF' little-endian
constexpr u32 kHdsrfVersion = 1;

enum class HdsrfFormat : u32 {
    Pcm16 = 0, // 16-bit little-endian PCM chunks
};

struct HdsrfHeader {
    u32 version = kHdsrfVersion;
    u32 sampleRate = 0;
    u32 channels = 0;
    u64 totalFrames = 0;
    u32 chunkFrames = 0;              // frames per chunk (last chunk may be shorter)
    u32 chunkCount = 0;
    HdsrfFormat format = HdsrfFormat::Pcm16;
    std::vector<u64> chunkOffsets;    // byte offset of each chunk from the start of the file
};

constexpr u32 kHdsrfDefaultChunkFrames = 8192;

// Cook interleaved f32 source PCM into an .HDSRF blob (appended to `out`). chunkFrames 0 -> default.
Result CookHdsrf(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate,
                 u32 chunkFrames, std::vector<u8>& out);

// Parse + validate just the header + seek table (cheap; for streaming setup). Rejects a bad
// magic/version/format or an out-of-bounds seek table.
Result ReadHdsrfHeader(const u8* data, usize size, HdsrfHeader& out);

// Decode the entire file into an in-memory f32 AudioBuffer.
Result ReadHdsrf(const u8* data, usize size, AudioBuffer& out);

// Decode ONE chunk (chunkIndex) to interleaved f32 in `out` (capacity outCapFrames*channels).
// Returns the number of frames written via *framesOut. For streaming.
Result DecodeHdsrfChunk(const u8* data, usize size, const HdsrfHeader& header, u32 chunkIndex,
                        f32* out, u32 outCapFrames, u32* framesOut);

// A bounded-memory streaming reader over an .HDSRF blob: it holds only the header + ONE decoded
// chunk at a time (not the whole file), decoding chunks on demand as playback advances. This is the
// building block for streaming voices (a background thread pulling Read() into a ring). The blob is
// referenced, not copied - the caller must keep it alive for the stream's lifetime.
class HdsrfStream {
public:
    Result Open(const u8* data, usize size); // parse header + seek table
    bool IsOpen() const { return data_ != nullptr; }
    u32 Channels() const { return header_.channels; }
    u32 SampleRate() const { return header_.sampleRate; }
    u64 TotalFrames() const { return header_.totalFrames; }
    u64 Position() const { return pos_; }
    void Seek(u64 frame);
    bool AtEnd() const { return pos_ >= header_.totalFrames; }

    // Read up to `frames` interleaved f32 frames into `out` (capacity frames*Channels()). Loops when
    // `loop` (never reports end). Returns the number of frames written (< frames only at end when
    // not looping). Decodes at most one new chunk per chunk boundary crossed.
    u32 Read(f32* out, u32 frames, bool loop);

private:
    const u8* data_ = nullptr;
    usize size_ = 0;
    HdsrfHeader header_;
    u64 pos_ = 0;                  // current frame
    u32 loadedChunk_ = 0xFFFFFFFFu; // which chunk chunkBuf_ currently holds
    u32 loadedFrames_ = 0;        // valid frames in chunkBuf_
    std::vector<f32> chunkBuf_;   // ONE chunk's worth of samples (bounded memory)
};

} // namespace reverie
