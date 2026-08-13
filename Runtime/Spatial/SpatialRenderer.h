// Reverie/Runtime/Spatial/SpatialRenderer.h - the spatial-audio backend abstraction.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Spatial rendering is pluggable. Reverie's runtime talks only to this interface; a concrete
// backend (a dependency-free panning renderer, or HDS Resonance for HRTF binaural + shoebox
// room acoustics) implements it. So spatial audio is a first-class feature that never hard-binds
// Reverie to any one DSP library.
//
// Contract: one renderer instance = one listener + one mixed stereo output for all its sources
// (this mirrors what a single-instance binaural renderer like Resonance provides). Per audio
// block: BeginBlock -> (per active source) SetSource + SubmitSourceAudio -> Render(stereoOut).
// Sources are a fixed pool: AcquireSource / ReleaseSource claim and free a slot.
#pragma once

#include "Core/Types.h"

namespace reverie {

enum class SpatialQuality : u32 {
    Panning = 0,       // amplitude panning (backends without HRTF map everything here)
    BinauralLow = 1,   // HRTF, low order  (Resonance: 1st-order ambisonic)
    BinauralMedium = 2, // HRTF, medium     (Resonance: 2nd-order)
    BinauralHigh = 3,  // HRTF, high order  (Resonance: 3rd-order)
};

// A shoebox acoustic environment for room reflections/reverb. Panning backends ignore it;
// Resonance maps it to its room model (per-wall materials + reverb).
struct AcousticEnvironment {
    bool enabled = false;
    Float3 position;                       // room center (world)
    Quat rotation;                         // room orientation
    Float3 dimensions{10.0f, 4.0f, 10.0f}; // extents (metres)
    u32 wallMaterials[6] = {0, 0, 0, 0, 0, 0}; // per-wall material index (backend-defined)
    f32 reverbGain = 1.0f;
};

class ISpatialRenderer {
public:
    virtual ~ISpatialRenderer() = default;

    virtual Result Init(u32 sampleRate, u32 maxSources) = 0;
    virtual void Shutdown() = 0;
    virtual const char* Name() const = 0;
    virtual u32 Capacity() const = 0;

    // Fixed source pool. Returns a slot id, or -1 when full.
    virtual int AcquireSource(SpatialQuality quality) = 0;
    virtual void ReleaseSource(int slot) = 0;

    virtual void SetListener(const Float3& position, const Float3& forward, const Float3& up) = 0;
    virtual void SetSource(int slot, const Float3& position, f32 volume, f32 minDistance,
                           f32 maxDistance) = 0;
    virtual void SetSourceOcclusion(int slot, f32 occlusion01) = 0; // 0 = clear, 1 = fully blocked
    virtual void SetSourceSpread(int slot, f32 spreadDegrees) = 0;
    virtual void SetEnvironment(const AcousticEnvironment& env) = 0;

    // Per-block audio path.
    virtual void BeginBlock(u32 frameCount) = 0;                       // size/clear accumulators
    virtual void SubmitSourceAudio(int slot, const f32* mono, u32 frameCount) = 0;
    virtual void Render(f32* stereoOut, u32 frameCount) = 0;           // writes the stereo mix
};

} // namespace reverie
