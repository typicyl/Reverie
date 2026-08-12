// Reverie/Runtime/Voices/Voice.h - a single playing voice.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A voice is one active playback of a sound. It plays from an in-memory AudioBuffer with
// per-voice gain, pitch (playback-rate multiplier), and looping; a fractional cursor linearly
// resamples a source whose rate differs from the device. The voice-management fields
// (priority / virtualized / owning instance / concurrency group / age) let the VoiceManager
// keep only the most important voices audible in a large scene. Streaming voices arrive with
// the asset/bank phase; this is the seam.
#pragma once

#include "Audio/AudioBuffer.h"
#include "Core/Types.h"

#include <memory>

namespace reverie {

enum class VoiceState : u8 {
    Free = 0, // slot finished / reaped
    Playing,
};

struct Voice {
    std::shared_ptr<const AudioBuffer> buffer; // in-memory source
    f64 cursor = 0.0;                          // fractional frame position in the source
    f32 volume = 1.0f;
    f32 pitch = 1.0f; // playback-rate multiplier (1 = source rate; >1 = higher/faster)
    bool loop = false;
    VoiceState state = VoiceState::Free;
    VoiceId id = kInvalidId;

    // --- voice management ---
    i32 priority = 0;            // higher = more important (kept audible under a voice budget)
    bool virtualized = false;    // true = over budget: cursor advances but no audio is mixed
    InstanceId eventInstance = 0; // owning event instance (0 = standalone voice)
    u32 concurrencyGroup = 0;    // shared limit key (0 = none)
    u64 age = 0;                 // spawn order; tie-breaks priority and drives steal-oldest
};

} // namespace reverie
