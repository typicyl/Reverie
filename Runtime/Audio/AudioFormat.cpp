// Reverie/Runtime/Audio/AudioFormat.cpp - see AudioFormat.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Audio/AudioFormat.h"

#include <cstring>

namespace reverie {

u32 BytesPerSample(SampleFormat fmt) {
    switch (fmt) {
        case SampleFormat::U8:  return 1;
        case SampleFormat::S16: return 2;
        case SampleFormat::S24: return 3;
        case SampleFormat::S32: return 4;
        case SampleFormat::F32: return 4;
        case SampleFormat::Unknown: default: return 0;
    }
}

void ConvertToF32(const void* src, SampleFormat srcFmt, f32* dst, usize sampleCount) {
    switch (srcFmt) {
        case SampleFormat::F32:
            std::memcpy(dst, src, sampleCount * sizeof(f32));
            break;
        case SampleFormat::U8: {
            const u8* s = static_cast<const u8*>(src);
            for (usize i = 0; i < sampleCount; ++i)
                dst[i] = (static_cast<f32>(s[i]) - 128.0f) / 128.0f;
            break;
        }
        case SampleFormat::S16: {
            const i16* s = static_cast<const i16*>(src);
            for (usize i = 0; i < sampleCount; ++i)
                dst[i] = static_cast<f32>(s[i]) / 32768.0f;
            break;
        }
        case SampleFormat::S24: {
            const u8* s = static_cast<const u8*>(src);
            for (usize i = 0; i < sampleCount; ++i) {
                const u8* p = s + i * 3;
                // little-endian 24-bit -> sign-extended 32-bit
                i32 v = static_cast<i32>((static_cast<u32>(p[0]) << 8) |
                                         (static_cast<u32>(p[1]) << 16) |
                                         (static_cast<u32>(p[2]) << 24));
                v >>= 8;
                dst[i] = static_cast<f32>(v) / 8388608.0f;
            }
            break;
        }
        case SampleFormat::S32: {
            const i32* s = static_cast<const i32*>(src);
            for (usize i = 0; i < sampleCount; ++i)
                dst[i] = static_cast<f32>(static_cast<f64>(s[i]) / 2147483648.0);
            break;
        }
        case SampleFormat::Unknown:
        default:
            std::memset(dst, 0, sampleCount * sizeof(f32));
            break;
    }
}

} // namespace reverie
