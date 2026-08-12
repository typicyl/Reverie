// Reverie/SDK/Include/Reverie/Reverie.h - the public C++ SDK facade.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The stable, ABI-lean C++ surface a host links against. It exposes only Reverie's POD
// vocabulary (Core/Types.h: Result + plain ids/vectors) and an opaque, pimpl'd Engine; no
// runtime internals, no miniaudio, no backend type leaks through. Language bindings should
// prefer the flat C ABI in <reverie.h>; this header is the ergonomic C++ path.
//
//   reverie::Engine engine;
//   engine.Init({});
//   reverie::SoundId fire = engine.LoadSoundFile("Weapons/Rifle_Fire.wav");
//   engine.Play(fire);
#pragma once

#include "Core/Types.h"

#include <memory>

namespace reverie {

// Public mirror of the output backend (kept separate from the internal DeviceBackend so the
// runtime enum can evolve without breaking the SDK).
enum class Backend : u32 {
    Null = 0,      // headless: driven via RenderOffline (tests / CI / servers / offline render)
    Miniaudio = 1, // real playback device
};

struct Config {
    Backend backend = Backend::Miniaudio;
    u32 sampleRate = 48000;
    u32 channels = 2; // output channels (Phase 1: stereo)
    u32 periodFrames = 0;
};

// The audio engine. One per game/app instance. Not copyable.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Result Init(const Config& config);
    void Shutdown();
    bool IsInitialized() const;

    // Real device control (no-op on the Null backend, which is pulled via RenderOffline).
    Result Start();
    Result Stop();

    void SetMasterVolume(f32 volume);
    f32 MasterVolume() const;
    u32 OutputChannels() const;
    u32 OutputSampleRate() const;

    // Loads a sound into memory. From an encoded file (WAV/FLAC/MP3) or from raw interleaved
    // f32 PCM. Returns kInvalidId on failure.
    SoundId LoadSoundFile(const char* path);
    SoundId LoadSoundPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate);
    void UnloadSound(SoundId sound);

    // Plays a loaded sound. Returns a voice id (kInvalidId on failure). A playing voice keeps
    // its sound alive even if UnloadSound is called.
    VoiceId Play(SoundId sound, f32 volume = 1.0f, bool loop = false);
    void StopVoice(VoiceId voice);
    void StopAll();
    u32 ActiveVoiceCount() const;

    // Offline pull of the whole graph (interleaved f32, OutputChannels()). For the Null
    // backend / tests / the offline renderer. Returns frames written.
    u32 RenderOffline(f32* out, u32 frameCount);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reverie
