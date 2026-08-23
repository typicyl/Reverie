// Reverie/Runtime/Voices/Voice.h - a single slot in the lock-free voice pool.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A voice is one active playback of a sound. It plays from an in-memory AudioBuffer with per-voice
// gain, pitch (playback-rate multiplier), and looping; a fractional cursor linearly resamples a
// source whose rate differs from the device.
//
// Concurrency: voices live in a FIXED, pre-allocated pool (VoiceManager) that is never reallocated,
// so the control thread and the audio thread can share slots without a lock. `state` is the ONLY
// cross-thread synchronization point - a small state machine that gates who may touch the rest of
// the slot and when:
//
//   Free  --(control: Play fills the slot, then publishes Playing[release])-->  Playing
//   Playing --(control Stop*: CAS Playing->Stopping)-->  Stopping  --(audio: teardown, Free)--> Free
//   Playing --(audio: voice finished -> teardown, Free)-------------------------------------->  Free
//
// Rules that make the lock-free sharing sound:
//   * Free slots are invisible to the audio thread; only the (single) control thread claims them.
//   * The control thread fills a slot's data BEFORE storing Playing (release); the audio thread
//     reads that data only AFTER loading Playing (acquire) -> happens-before covers `buffer` etc.
//   * Teardown (spatial-source release + publishing Free) is completed ONLY by the audio thread in
//     the mix pass, so a slot never becomes reusable while the audio thread is still touching it.
//     (Shutdown is the sole exception: the device is stopped first, so the control thread may tear
//      down synchronously - see VoiceManager::ReleaseAllForShutdown.)
//   * The buffer's shared_ptr is dropped on the CONTROL thread when a Free slot is reused, never on
//     the audio thread - so no large heap free ever runs in the render callback.
#pragma once

#include "Audio/AudioBuffer.h"
#include "Core/Types.h"
#include "Spatial/SpatialRenderer.h" // SpatialQuality

#include <atomic>
#include <memory>

namespace reverie {

enum class VoiceState : u8 {
    Free = 0,     // unused; the control thread may claim it
    Playing = 1,  // live; the audio thread mixes it
    Stopping = 2, // stop requested; the audio thread will tear it down and publish Free
};

struct Voice {
    // The synchronization point (see file header). All loads/stores use explicit ordering.
    std::atomic<u8> state{static_cast<u8>(VoiceState::Free)};
    // Over-budget flag: written by the control thread (ReprioritizeControl on Play/Stop), read by
    // the audio thread each block. Relaxed - a one-block-stale value only mixes/skips one extra
    // block of one voice, never a correctness or safety hazard.
    std::atomic<u8> virtualizedFlag{0};

    std::shared_ptr<const AudioBuffer> buffer; // control writes while Free; audio reads while Playing
    f64 cursor = 0.0;                          // AUDIO-thread owned (fractional frame position)
    f32 volume = 1.0f;
    f32 pitch = 1.0f; // playback-rate multiplier (1 = source rate; >1 = higher/faster)
    bool loop = false;
    VoiceId id = kInvalidId;

    BusId bus = kInvalidId; // routing target (kInvalidId = Master, resolved by the mixer)

    // --- spatial (3D) ---
    bool spatial = false; // wants spatial rendering (the source slot is acquired lazily on the audio thread)
    Float3 position;      // world position; control may update via SetVoicePosition (benign torn read)
    int spatialSlot = -1; // spatial renderer source slot (>=0 once acquired; acquired in MixToBuses)
    SpatialQuality quality = SpatialQuality::Panning; // requested quality (used when the slot is acquired)
    f32 minDistance = 1.0f;
    f32 maxDistance = 100.0f;

    // --- streaming ---
    int streamSlot = -1; // >=0: this voice pulls from a StreamManager slot instead of `buffer`

    // --- parameter modulation (RTPC) ---
    // When volumeParam != kInvalidId, the audio thread multiplies `volume` by
    // smoothstep(paramLo, paramHi, parameterValue) each block - the mechanism behind music-layer
    // gains and general volume automation. Read-only on the audio thread; set once at Play.
    ParameterId volumeParam = kInvalidId;
    f32 paramLo = 0.0f;
    f32 paramHi = 1.0f;

    // --- voice management ---
    i32 priority = 0;             // higher = more important (kept audible under a voice budget)
    InstanceId eventInstance = 0; // owning event instance (0 = standalone voice)
    u32 concurrencyGroup = 0;     // shared limit key (0 = none)
    u64 age = 0;                  // spawn order; tie-breaks priority

    Voice() = default;
    Voice(const Voice&) = delete;            // atomics make this non-copyable/non-movable, which is
    Voice& operator=(const Voice&) = delete; // why the pool is a fixed unique_ptr<Voice[]>, not a vector
};

} // namespace reverie
