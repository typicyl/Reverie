// Reverie/Runtime/Music/MusicSystem.h - adaptive music: states, layers, a musical clock.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A music STATE is a set of synchronized looping LAYERS (base + percussion + strings + ...). Each
// layer's gain can be driven by a game parameter (RTPC) via smoothstep, so intensity ramps a layer
// in/out without the game touching audio. SetState switches states (immediate in this first slice;
// beat/bar-quantized transitions build on MusicClock next). The musical clock advances on the audio
// thread and is published for the control thread (for future quantization + UI).
//
// Threading: RegisterState/SetState/Stop and queries are control-thread; Update advances the clock
// on the audio thread. State changes spawn/stop voices on the control thread (VoiceManager::Play/
// Stop); the audio thread only advances the clock and (via the voice pool's parameter modulation)
// applies layer gains. Tempo + current-state cross the boundary as atomics.
#pragma once

#include "Audio/AudioBuffer.h"
#include "Core/Types.h"
#include "Music/MusicClock.h"
#include "Voices/VoiceManager.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace reverie {

struct MusicLayerDef {
    SoundId sound = kInvalidId;
    f32 gain = 1.0f;                    // base gain
    ParameterId gainParam = kInvalidId; // optional: gain *= smoothstep(paramLo,paramHi, param)
    f32 paramLo = 0.0f;
    f32 paramHi = 1.0f;
};

struct MusicStateDef {
    std::string name;
    f32 bpm = 120.0f;
    u32 beatsPerBar = 4;
    std::vector<MusicLayerDef> layers;
};

class MusicSystem {
public:
    using ResolveSound = std::function<std::shared_ptr<const AudioBuffer>(SoundId)>;

    MusicSystem(VoiceManager& voices, ResolveSound resolveSound);

    void SetBus(BusId bus) { bus_ = bus; }

    MusicStateId RegisterState(const MusicStateDef& def); // control; kInvalidId on bad input
    MusicStateId FindState(const char* name) const;
    // Switch state. Immediate happens now; NextBeat/NextBar defer to the boundary and are applied by
    // Tick() (call it from the host's per-frame Update). With no state playing, always immediate.
    void SetState(MusicStateId id, MusicTransition transition = MusicTransition::Immediate);
    void Stop();                    // control: stop all music
    MusicStateId CurrentState() const { return current_.load(std::memory_order_relaxed); }

    // Control-thread per-frame pump: applies a pending quantized transition once the musical clock
    // has reached its boundary. Cheap no-op when nothing is pending.
    void Tick();

    // Audio thread, once per block: advance the musical clock and publish its position.
    void Update(f64 blockSeconds);

    // Published clock queries (control thread).
    f64 CurrentBeat() const { return publishedBeats_.load(std::memory_order_relaxed); }
    u64 CurrentBar() const { return publishedBar_.load(std::memory_order_relaxed); }
    f32 Bpm() const { return bpm_.load(std::memory_order_relaxed); }

private:
    const MusicStateDef* FindDef(MusicStateId id) const;
    void SwitchNow(MusicStateId id); // control: actually stop old + start new layers

    VoiceManager& voices_;
    ResolveSound resolveSound_;
    BusId bus_ = kInvalidId;

    std::unordered_map<MusicStateId, MusicStateDef> states_;
    std::unordered_map<std::string, MusicStateId> byName_;
    MusicStateId nextId_ = 1;
    std::vector<VoiceId> activeVoices_; // current state's layer voices (control-owned)

    // Pending quantized transition (control-thread only: set in SetState, applied in Tick).
    MusicStateId pendingState_ = kInvalidId;
    f64 pendingBoundaryBeat_ = 0.0;

    MusicClock clock_; // audio-thread-owned
    std::atomic<MusicStateId> current_{kInvalidId};
    std::atomic<f32> bpm_{120.0f};
    std::atomic<u32> beatsPerBar_{4};
    std::atomic<f64> publishedBeats_{0.0};
    std::atomic<u64> publishedBar_{0};
};

} // namespace reverie
