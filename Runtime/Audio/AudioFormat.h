// Reverie/Runtime/Audio/AudioFormat.h - sample formats + PCM format description + conversion.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Reverie's own audio-format vocabulary. This deliberately does NOT expose miniaudio's
// ma_format / ma_format_* - the decoder/device backends translate to and from these types
// so miniaudio (or any future backend) stays an implementation detail. Reverie mixes
// internally in interleaved 32-bit float; everything converts to that at the edges.
#pragma once

#include "Core/Types.h"

namespace reverie {

// Interleaved PCM sample encodings Reverie can ingest. F32 is the internal mix format.
enum class SampleFormat : u32 {
    Unknown = 0,
    U8,   // unsigned 8-bit
    S16,  // signed 16-bit
    S24,  // signed 24-bit packed (3 bytes/sample)
    S32,  // signed 32-bit
    F32,  // 32-bit float [-1, 1]
};

u32 BytesPerSample(SampleFormat fmt);

// Describes an interleaved PCM stream (not the samples themselves).
struct AudioFormat {
    u32 channels = 2;
    u32 sampleRate = 48000;
    SampleFormat format = SampleFormat::F32;

    u32 BytesPerFrame() const { return BytesPerSample(format) * channels; }
    bool Valid() const {
        return channels > 0 && sampleRate > 0 && format != SampleFormat::Unknown;
    }
};

// Converts `sampleCount` interleaved samples of `srcFmt` into interleaved f32 [-1,1] in
// `dst`. `dst` must hold `sampleCount` floats. A no-op fast path when srcFmt is already F32.
// This is per-SAMPLE (channel-agnostic); channel/rate conversion happens elsewhere.
void ConvertToF32(const void* src, SampleFormat srcFmt, f32* dst, usize sampleCount);

} // namespace reverie
