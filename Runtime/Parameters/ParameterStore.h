// Reverie/Runtime/Parameters/ParameterStore.h - the generalized game-parameter (RTPC) store.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A parameter is a named, ranged, smoothed float the game drives ("CombatIntensity", "Health",
// "Speed"). It is the substrate every higher system consumes: adaptive music (layer gains,
// transitions), DSP automation, event/voice modulation, and mixer snapshots. The store is
// deliberately built now, before those consumers, so they all read one coherent value model.
//
// Threading: the control thread registers parameters and sets targets; the audio thread advances
// each parameter's smoothed CURRENT value toward its TARGET once per block (Advance). Values are
// std::atomic (no lock): a parameter value carries no happens-before obligation for other data, so
// relaxed access is correct and a one-block-stale read is harmless. Registration publishes a slot
// with release / reads it with acquire, exactly like the voice pool, so a parameter may be
// registered before playback (the normal case) or, safely, while the audio thread runs.
//
// Ids are 1-based slot indices (0 = invalid), so Set/Get are O(1) with no scan or map on the hot
// path; the name->id map is consulted only by Find on the control thread.
#pragma once

#include "Core/Types.h"

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace reverie {

class ParameterStore {
public:
    void Reserve(u32 capacity); // allocate the fixed parameter pool (call once at init)

    // Registers (or returns the existing id for) a named parameter. Control thread.
    // smoothMs is the time-constant for CURRENT to chase TARGET (0 = instant). kInvalidId if full.
    ParameterId Register(const char* name, f32 defaultValue, f32 minValue, f32 maxValue,
                         f32 smoothMs);

    ParameterId Find(const char* name) const; // kInvalidId if not registered (control thread)
    u32 Count() const { return count_; }

    // Enumeration (control thread) for serialization/inspection. index is dense in [0, Count()).
    struct Descriptor {
        std::string name;
        f32 defaultValue = 0.0f, minValue = 0.0f, maxValue = 1.0f, smoothMs = 0.0f;
    };
    bool DescribeAt(u32 index, Descriptor& out) const;

    void SetTarget(ParameterId id, f32 value); // clamps to [min,max]; control thread
    f32 Target(ParameterId id) const;
    f32 Value(ParameterId id) const;           // current smoothed value; any thread

    // Audio thread, once per block: advance every parameter's current value toward its target.
    void Advance(f32 blockSeconds);

private:
    struct Param {
        std::atomic<f32> current{0.0f};
        std::atomic<f32> target{0.0f};
        f32 minValue = 0.0f;
        f32 maxValue = 1.0f;
        f32 defaultValue = 0.0f;
        f32 smoothMs = 0.0f;
        std::atomic<u8> used{0}; // published (release) after the fields above are filled
    };

    Param* Slot(ParameterId id);             // nullptr if id invalid / unused
    const Param* Slot(ParameterId id) const;

    std::unique_ptr<Param[]> params_; // fixed pool; atomics make Param non-movable (hence not a vector)
    u32 capacity_ = 0;
    u32 count_ = 0; // registered count (control thread)
    std::unordered_map<std::string, ParameterId> byName_;
    std::vector<std::string> names_; // slot -> name, for enumeration (control thread)
};

} // namespace reverie
