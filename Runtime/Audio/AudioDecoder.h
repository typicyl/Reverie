// Reverie/Runtime/Audio/AudioDecoder.h - decoding + streaming (WAV/FLAC/MP3).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The "Assets -> Decoder/Streaming" stage of the pipeline. Two paths:
//   * AudioDecoder::Decode* fully decodes a file/blob into an in-memory AudioBuffer (f32) -
//     for short SFX.
//   * AudioStream pulls frames on demand from a file/blob - for music / large assets.
// The concrete decoder (miniaudio's ma_decoder today) is hidden in the .cpp; no ma_* type
// crosses this header, so the codec backend can be replaced without touching callers.
#pragma once

#include "Audio/AudioBuffer.h"
#include "Audio/AudioFormat.h"
#include "Core/Types.h"

#include <memory>

namespace reverie {

class AudioDecoder {
public:
    // Fully decode a file into `out` as interleaved f32 at its native rate/channels.
    static Result DecodeFile(const char* path, AudioBuffer& out);
    // Fully decode an in-memory encoded blob (WAV/FLAC/MP3). The blob is not retained.
    static Result DecodeMemory(const void* data, usize bytes, AudioBuffer& out);
};

// On-demand streaming decode for large/music assets. Not thread-safe; one reader.
class AudioStream {
public:
    AudioStream();
    ~AudioStream();
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    Result OpenFile(const char* path);
    // Opens an in-memory encoded blob. The bytes are COPIED (the stream owns its copy) so the
    // caller need not keep them alive.
    Result OpenMemory(const void* data, usize bytes);
    bool IsOpen() const;
    void Close();

    AudioFormat Format() const; // native decoded format (f32)
    u64 FrameCount() const;     // total frames if known, else 0

    // Reads up to `frameCount` interleaved f32 frames into `out`. Returns frames actually
    // read (< requested at end of stream).
    u32 ReadFrames(f32* out, u32 frameCount);
    Result Seek(u64 frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reverie
