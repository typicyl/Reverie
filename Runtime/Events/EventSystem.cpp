// Reverie/Runtime/Events/EventSystem.cpp - see EventSystem.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// EventSystem state (events_, instances_, rng_) is owned by the game thread: PlayEvent /
// RegisterEvent / Stop* are called from there. The audio thread never touches it - it only
// calls VoiceManager::Mix. So no mutex is needed here; the VoiceManager guards the shared
// voice list it hands audio to.
#include "Events/EventSystem.h"

#include <algorithm>

namespace reverie {

EventSystem::EventSystem(VoiceManager& voices, ResolveSound resolveSound)
    : voices_(voices), resolveSound_(std::move(resolveSound)) {}

void EventSystem::SetSeed(u64 seed) { rng_.seed(seed); }

EventId EventSystem::RegisterEvent(const AudioEventDef& def) {
    const EventId id = nextEvent_++;
    if (nextEvent_ == kInvalidId) nextEvent_ = 1;
    events_[id] = def;
    return id;
}

void EventSystem::UnregisterEvent(EventId event) { events_.erase(event); }

const AudioEventDef* EventSystem::Find(EventId event) const {
    auto it = events_.find(event);
    return it != events_.end() ? &it->second : nullptr;
}

f32 EventSystem::RandVariance(f32 amount) {
    if (amount <= 0.0f) return 0.0f;
    std::uniform_real_distribution<f32> dist(-amount, amount);
    return dist(rng_);
}

SoundId EventSystem::PickFromPool(const EventLayer& layer) {
    if (layer.pool.empty()) return kInvalidId;
    f32 total = 0.0f;
    for (const EventSound& e : layer.pool)
        if (e.weight > 0.0f) total += e.weight;
    if (total <= 0.0f) return layer.pool.front().sound; // all weightless -> first entry
    std::uniform_real_distribution<f32> dist(0.0f, total);
    f32 r = dist(rng_);
    for (const EventSound& e : layer.pool) {
        if (e.weight <= 0.0f) continue;
        r -= e.weight;
        if (r <= 0.0f) return e.sound;
    }
    return layer.pool.back().sound;
}

void EventSystem::PruneDeadInstances() {
    instances_.erase(
        std::remove_if(instances_.begin(), instances_.end(),
                       [this](const ActiveInstance& a) {
                           return voices_.InstanceVoiceCount(a.id) == 0;
                       }),
        instances_.end());
}

InstanceId EventSystem::PlayEvent(EventId event, f32 volume, bool spatial,
                                  const Float3& position) {
    const AudioEventDef* def = Find(event);
    if (def == nullptr) return 0;

    PruneDeadInstances();

    // Concurrency: cap live instances of this event by stealing the oldest one.
    if (def->maxInstances > 0) {
        for (;;) {
            u32 live = 0;
            const ActiveInstance* oldest = nullptr;
            for (const ActiveInstance& a : instances_) {
                if (a.event != event) continue;
                ++live;
                if (oldest == nullptr || a.age < oldest->age) oldest = &a;
            }
            if (live < def->maxInstances || oldest == nullptr) break;
            StopInstance(oldest->id); // steal-oldest
        }
    }

    const InstanceId instance = nextInstance_++;
    if (nextInstance_ == 0) nextInstance_ = 1;

    bool anySpawned = false;
    for (const EventLayer& layer : def->layers) {
        if (layer.probability < 1.0f) {
            std::uniform_real_distribution<f32> roll(0.0f, 1.0f);
            if (roll(rng_) >= layer.probability) continue;
        }
        const SoundId picked = PickFromPool(layer);
        if (picked == kInvalidId) continue;
        std::shared_ptr<const AudioBuffer> buffer = resolveSound_ ? resolveSound_(picked) : nullptr;
        if (!buffer) continue;

        VoiceSpawn spawn;
        spawn.buffer = std::move(buffer);
        spawn.volume = layer.volume * volume * (1.0f + RandVariance(layer.volumeVariance));
        if (spawn.volume < 0.0f) spawn.volume = 0.0f;
        spawn.pitch = layer.pitch * (1.0f + RandVariance(layer.pitchVariance));
        spawn.loop = layer.loop;
        spawn.priority = def->priority;
        spawn.eventInstance = instance;
        spawn.concurrencyGroup = def->concurrencyGroup;
        spawn.bus = layer.bus;
        spawn.spatial = spatial;
        spawn.position = position;
        if (voices_.Play(spawn) != kInvalidId) anySpawned = true;
    }

    if (!anySpawned) return 0; // probability/pool produced nothing; no live instance

    instances_.push_back(ActiveInstance{instance, event, nextAge_++});
    return instance;
}

void EventSystem::StopInstance(InstanceId instance) {
    if (instance == 0) return;
    voices_.StopInstance(instance);
    instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
                                    [instance](const ActiveInstance& a) { return a.id == instance; }),
                     instances_.end());
}

void EventSystem::StopAllInstances() {
    for (const ActiveInstance& a : instances_) voices_.StopInstance(a.id);
    instances_.clear();
}

u32 EventSystem::ActiveInstanceCount(EventId event) const {
    u32 n = 0;
    for (const ActiveInstance& a : instances_)
        if (a.event == event && voices_.InstanceVoiceCount(a.id) > 0) ++n;
    return n;
}

u32 EventSystem::TotalInstanceCount() const { return static_cast<u32>(instances_.size()); }

} // namespace reverie
