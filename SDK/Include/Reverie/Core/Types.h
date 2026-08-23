// Reverie/Runtime/Core/Types.h - fundamental engine-agnostic types.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Reverie is a standalone, engine-agnostic audio middleware runtime. This header defines
// the small vocabulary of POD types the whole runtime shares. There is deliberately NO
// dependency on any game engine, math library (glm), ECS, or serialization framework here -
// the public API and the runtime speak only in these plain types.
#pragma once

#include <cstddef> // std::size_t
#include <cstdint>

namespace reverie {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;

// Plain 3D vector / quaternion. Reverie uses its own POD math so a consumer with a
// different math library (glm, DirectXMath, Unreal's FVector, Unity's Vector3) is never
// forced onto ours; adapters convert at the boundary. Right-handed world space by
// convention (matching the Resonance spatial backend).
struct Float3 {
    f32 x = 0.0f, y = 0.0f, z = 0.0f;
};
struct Quat {
    f32 x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; // (x,y,z,w)
};

// Result codes. Every fallible entry point returns one of these instead of throwing, so
// the C ABI can mirror them 1:1 and bindings in any language can check them.
enum class Result : i32 {
    Ok = 0,
    Error = -1,
    InvalidArgument = -2,
    NotInitialized = -3,
    AlreadyInitialized = -4,
    DeviceError = -5,
    DecodeError = -6,
    FileNotFound = -7,
    Unsupported = -8,
    OutOfMemory = -9,
    NotFound = -10,
};

inline bool Succeeded(Result r) { return r == Result::Ok; }
inline bool Failed(Result r) { return r != Result::Ok; }

// A snapshot of runtime state for profiling/debugging (cheap to fetch; read from atomics).
struct EngineStats {
    u32 sampleRate = 0;
    u32 channels = 0;
    u32 activeVoices = 0;  // real + virtual
    u32 realVoices = 0;    // currently mixed
    u32 virtualVoices = 0; // over budget, silent
    f32 cpuLoad = 0.0f;    // last block: render time / block duration (0..1 = fraction of budget)
    f32 masterPeak = 0.0f; // last block's Master peak level
    f64 musicBeat = 0.0;
    u64 musicBar = 0;
    f32 musicBpm = 0.0f;
};

// DSP effect kinds that can be inserted on a mixer bus (public vocabulary).
enum class EffectType : u32 {
    Filter = 0,     // biquad filter; params: 0=FilterType, 1=cutoffHz, 2=Q, 3=gainDb
    Compressor = 1, // compressor/limiter; params: 0=threshDb,1=ratio,2=attackMs,3=releaseMs,4=makeupDb
    Delay = 2,      // feedback delay; params: 0=delayMs, 1=feedback, 2=wet mix
};

// When a music-state change takes effect.
enum class MusicTransition : u32 {
    Immediate = 0, // switch now
    NextBeat = 1,  // switch at the next beat boundary
    NextBar = 2,   // switch at the next bar boundary
};

// Biquad filter response (the value of a Filter effect's param 0).
enum class FilterType : u32 {
    Lowpass = 0,
    Highpass = 1,
    Bandpass = 2,
    Notch = 3,
    Peak = 4,
    LowShelf = 5,
    HighShelf = 6,
};

// Opaque runtime handle ids. 0 is always the invalid handle. Public handles are these plain
// integers so they cross the C ABI unchanged; the runtime maps them to internal objects.
// Allocation is a monotonic counter that skips 0, so ids are not recycled and a stale handle
// does not alias a live object in practice - until the 32-bit counter wraps after ~4 billion
// allocations (only then could a very long-lived object's id be reused). A generation-guarded,
// index-packed handle (which would also make lookups O(1)) is a planned hardening; today the
// runtime does linear-scan lookups by id.
using SoundId = u32;
using VoiceId = u32;
using BusId = u32;
using ParameterId = u32;
using EventId = u32;
using InstanceId = u32; // a playing event instance (a group of layer voices)
using EffectId = u32;   // a DSP effect inserted on a bus
using MusicStateId = u32; // a registered adaptive-music state

constexpr u32 kInvalidId = 0;

} // namespace reverie
