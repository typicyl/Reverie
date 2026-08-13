// Reverie/Runtime/Events/AudioEvent.h - the layered event definition.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// An event is the unit a game triggers ("Weapons.Rifle.Fire"). It is built from LAYERS that
// play together, e.g. Mechanical + MuzzleBlast + Shell + Tail + LowFrequencyImpact. Each layer
// draws from a weighted pool of sounds (so repeats vary), with independent gain/pitch and their
// random variance, a trigger probability, and looping. This is the authoring-side data the
// runtime consumes; per-layer routing / DSP / sends / conditions / spatialization are reserved
// for later phases and are intentionally NOT stubbed here.
#pragma once

#include "Core/Types.h"

#include <string>
#include <vector>

namespace reverie {

// One weighted entry in a layer's sound pool. (The public SDK mirror is
// reverie::EventPoolEntry in <Reverie/Reverie.h>; this is the runtime type.)
struct EventSound {
    SoundId sound = kInvalidId;
    f32 weight = 1.0f; // relative selection weight (>0)
};

struct EventLayer {
    std::string name;
    std::vector<EventSound> pool; // weighted-random pick per trigger (empty layer = silent)
    f32 volume = 1.0f;
    f32 volumeVariance = 0.0f; // +/- linear gain applied randomly per trigger (0 = none)
    f32 pitch = 1.0f;          // playback-rate multiplier
    f32 pitchVariance = 0.0f;  // +/- pitch applied randomly per trigger
    bool loop = false;
    f32 probability = 1.0f;    // 0..1 chance this layer triggers at all
};

struct AudioEventDef {
    std::string name;             // "Weapons.Rifle.Fire"
    std::vector<EventLayer> layers;
    i32 priority = 0;             // base priority handed to every voice this event spawns
    u32 maxInstances = 0;         // 0 = unlimited; else steal the oldest instance when exceeded
    u32 concurrencyGroup = 0;     // shared-limit key stamped on this event's voices (0 = none)
};

} // namespace reverie
