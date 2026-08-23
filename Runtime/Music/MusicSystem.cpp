// Reverie/Runtime/Music/MusicSystem.cpp - see MusicSystem.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Music/MusicSystem.h"

#include <cmath>

namespace reverie {

MusicSystem::MusicSystem(VoiceManager& voices, ResolveSound resolveSound)
    : voices_(voices), resolveSound_(std::move(resolveSound)) {}

MusicStateId MusicSystem::RegisterState(const MusicStateDef& def) {
    if (def.name.empty()) return kInvalidId;
    if (MusicStateId existing = FindState(def.name.c_str()); existing != kInvalidId) {
        states_[existing] = def; // update in place
        return existing;
    }
    const MusicStateId id = nextId_++;
    if (nextId_ == kInvalidId) nextId_ = 1;
    states_[id] = def;
    byName_[def.name] = id;
    return id;
}

MusicStateId MusicSystem::FindState(const char* name) const {
    if (name == nullptr) return kInvalidId;
    auto it = byName_.find(name);
    return it != byName_.end() ? it->second : kInvalidId;
}

const MusicStateDef* MusicSystem::FindDef(MusicStateId id) const {
    auto it = states_.find(id);
    return it != states_.end() ? &it->second : nullptr;
}

void MusicSystem::SetState(MusicStateId id, MusicTransition transition) {
    if (FindDef(id) == nullptr) return;
    // Immediate, or nothing playing yet (no clock to quantize against) -> switch now.
    if (transition == MusicTransition::Immediate ||
        current_.load(std::memory_order_relaxed) == kInvalidId) {
        pendingState_ = kInvalidId;
        SwitchNow(id);
        return;
    }
    // Defer to the next beat/bar boundary; Tick() applies it once the clock reaches it.
    const f64 grid = (transition == MusicTransition::NextBar)
                         ? static_cast<f64>(beatsPerBar_.load(std::memory_order_relaxed))
                         : 1.0;
    const f64 cur = publishedBeats_.load(std::memory_order_relaxed);
    const f64 nextBoundary = (std::floor(cur / grid) + 1.0) * grid;
    pendingState_ = id;
    pendingBoundaryBeat_ = nextBoundary;
}

void MusicSystem::Tick() {
    if (pendingState_ == kInvalidId) return;
    if (publishedBeats_.load(std::memory_order_relaxed) >= pendingBoundaryBeat_) {
        const MusicStateId id = pendingState_;
        pendingState_ = kInvalidId;
        SwitchNow(id);
    }
}

void MusicSystem::SwitchNow(MusicStateId id) {
    const MusicStateDef* def = FindDef(id);
    if (def == nullptr) return;

    // Stop the current state's layer voices (hard switch; crossfade is a later refinement).
    for (VoiceId v : activeVoices_) voices_.Stop(v);
    activeVoices_.clear();

    // Adopt the new tempo (read by the audio thread's Update).
    bpm_.store(def->bpm, std::memory_order_relaxed);
    beatsPerBar_.store(def->beatsPerBar, std::memory_order_relaxed);

    // Spawn one looping, high-priority voice per layer, gain-modulated by its parameter.
    for (const MusicLayerDef& layer : def->layers) {
        std::shared_ptr<const AudioBuffer> buf = resolveSound_ ? resolveSound_(layer.sound) : nullptr;
        if (!buf) continue;
        VoiceSpawn s;
        s.buffer = std::move(buf);
        s.loop = true;
        s.bus = bus_;
        s.volume = layer.gain;
        s.priority = 1000; // music should not be voice-stolen by SFX
        s.volumeParam = layer.gainParam;
        s.paramLo = layer.paramLo;
        s.paramHi = layer.paramHi;
        const VoiceId v = voices_.Play(s);
        if (v != kInvalidId) activeVoices_.push_back(v);
    }
    current_.store(id, std::memory_order_relaxed);
}

void MusicSystem::Stop() {
    pendingState_ = kInvalidId; // cancel any pending quantized transition
    for (VoiceId v : activeVoices_) voices_.Stop(v);
    activeVoices_.clear();
    current_.store(kInvalidId, std::memory_order_relaxed);
}

void MusicSystem::Update(f64 blockSeconds) {
    clock_.Configure(bpm_.load(std::memory_order_relaxed), beatsPerBar_.load(std::memory_order_relaxed));
    clock_.SetPlaying(current_.load(std::memory_order_relaxed) != kInvalidId);
    clock_.Advance(blockSeconds);
    publishedBeats_.store(clock_.TotalBeats(), std::memory_order_relaxed);
    publishedBar_.store(clock_.Bar(), std::memory_order_relaxed);
}

} // namespace reverie
