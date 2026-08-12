// Reverie/Runtime/Audio/AudioBuffer.h - a fully decoded sound held in memory.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The in-memory PCM representation Reverie plays from for short assets (SFX). Large/music
// assets stream instead (AudioStream). Samples are always interleaved 32-bit float at the
// buffer's native sample rate; the voice mixer resamples/upmixes to the device format.
#pragma once

#include "Audio/AudioFormat.h"
#include "Core/Types.h"

#include <vector>

namespace reverie {

struct AudioBuffer {
    std::vector<f32> samples; // interleaved f32
    u32 channels = 0;
    u32 sampleRate = 0;

    u32 FrameCount() const {
        return channels != 0 ? static_cast<u32>(samples.size() / channels) : 0;
    }
    bool Empty() const { return samples.empty() || channels == 0; }
    AudioFormat Format() const { return AudioFormat{channels, sampleRate, SampleFormat::F32}; }
};

} // namespace reverie
