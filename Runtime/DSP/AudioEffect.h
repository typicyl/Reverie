// Reverie/Runtime/DSP/AudioEffect.h - the real-time DSP effect interface.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// An effect is an in-place, real-time-safe processor inserted into a mixer bus's signal chain.
// Process() runs on the audio thread and MUST NOT allocate, lock, or block - any state (filter
// memory, delay lines) is allocated once in Prepare(). Parameters are addressed by index so the
// public API can drive built-in effects generically and later bind them to game parameters (RTPC).
#pragma once

#include "Core/Types.h" // EffectType / FilterType live here (public vocabulary)

namespace reverie {

class IAudioEffect {
public:
    virtual ~IAudioEffect() = default;

    // Allocate/prepare state for this format. Control thread, before the effect is processed.
    virtual void Prepare(u32 sampleRate, u32 channels) = 0;
    virtual void Reset() = 0; // clear internal state (silence memory), keeping parameters

    // In-place processing of an interleaved block. Audio thread; real-time safe (no allocation).
    virtual void Process(f32* buffer, u32 frameCount, u32 channels) = 0;

    virtual void SetParam(u32 index, f32 value) = 0;
    virtual f32 GetParam(u32 index) const = 0;
    virtual EffectType Type() const = 0;
    virtual const char* Name() const = 0;
};

} // namespace reverie
