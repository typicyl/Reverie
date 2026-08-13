// Reverie/Runtime/Events/EventSystem.h - registers event defs, plays them as instances.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Turns an AudioEventDef into sound: on PlayEvent it evaluates each layer (probability roll,
// weighted-pool pick, gain/pitch variance) and spawns the resulting voices as ONE instance
// through the VoiceManager, tagging them with the instance id, event priority and concurrency
// group. Per-event `maxInstances` is enforced by stealing the oldest live instance. A seedable
// RNG makes the random choices reproducible for tests. Sounds are resolved through a callback so
// the EventSystem stays decoupled from where sounds are stored (the SDK owns the sound table).
#pragma once

#include "Audio/AudioBuffer.h"
#include "Core/Types.h"
#include "Events/AudioEvent.h"
#include "Voices/VoiceManager.h"

#include <functional>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

namespace reverie {

class EventSystem {
public:
    // `resolveSound` maps a SoundId to its decoded buffer (nullptr if missing/unloaded).
    using ResolveSound = std::function<std::shared_ptr<const AudioBuffer>(SoundId)>;

    EventSystem(VoiceManager& voices, ResolveSound resolveSound);

    void SetSeed(u64 seed);

    EventId RegisterEvent(const AudioEventDef& def); // copies def; kInvalidId on bad input
    void UnregisterEvent(EventId event);
    const AudioEventDef* Find(EventId event) const;

    // Plays an event; returns an instance id (0 on failure / nothing triggered). When `spatial`,
    // every layer voice is 3D at `position` (rendered by the spatial renderer).
    InstanceId PlayEvent(EventId event, f32 volume, bool spatial = false,
                         const Float3& position = Float3{});
    void StopInstance(InstanceId instance);
    void StopAllInstances();

    u32 ActiveInstanceCount(EventId event) const; // live instances of that event
    u32 TotalInstanceCount() const;

private:
    struct ActiveInstance {
        InstanceId id = 0;
        EventId event = kInvalidId;
        u64 age = 0;
    };

    void PruneDeadInstances();
    f32 RandVariance(f32 amount); // uniform in [-amount, +amount]
    SoundId PickFromPool(const EventLayer& layer);

    VoiceManager& voices_;
    ResolveSound resolveSound_;
    std::unordered_map<EventId, AudioEventDef> events_;
    std::vector<ActiveInstance> instances_;
    EventId nextEvent_ = 1;
    InstanceId nextInstance_ = 1;
    u64 nextAge_ = 1;
    std::mt19937_64 rng_{0x9E3779B97F4A7C15ull};
};

} // namespace reverie
