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

// Opaque runtime handle ids. 0 is always the invalid handle. Public handles are these
// plain integers so they cross the C ABI unchanged; the runtime maps them to internal
// objects (with a generation guard where relevant) so a stale handle can never alias a
// recycled slot.
using SoundId = u32;
using VoiceId = u32;
using BusId = u32;
using ParameterId = u32;
using EventId = u32;
using InstanceId = u32; // a playing event instance (a group of layer voices)

constexpr u32 kInvalidId = 0;

} // namespace reverie
