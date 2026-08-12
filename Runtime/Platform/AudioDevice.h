// Reverie/Runtime/Platform/AudioDevice.h - output device abstraction.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Reverie owns audio output. A device PULLS interleaved f32 frames from an IAudioRenderer
// (the engine) - on a real backend this happens on the backend's audio thread; on the Null
// backend it happens synchronously when the host asks. miniaudio lives ONLY inside the
// MiniaudioDevice .cpp; no ma_* type appears here, so the backend can be replaced without
// touching the public runtime or SDK.
#pragma once

#include "Audio/AudioFormat.h"
#include "Core/Types.h"

#include <memory>

namespace reverie {

// The engine implements this; the device calls it to fill output. `output` is interleaved
// f32 for `channels`. On a real device this runs on the audio thread and must be
// non-blocking; on Null/offline it runs on the caller's thread.
class IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;
    virtual void RenderAudio(f32* output, u32 frameCount, u32 channels, u32 sampleRate) = 0;
};

enum class DeviceBackend : u32 {
    // Headless: touches no hardware. The host pulls frames via RenderOffline (tests, CI,
    // dedicated servers, and - later - the offline file renderer).
    Null = 0,
    // Real playback via miniaudio (WASAPI/CoreAudio/ALSA/... selected by miniaudio).
    Miniaudio = 1,
};

struct DeviceDesc {
    DeviceBackend backend = DeviceBackend::Miniaudio;
    AudioFormat format;      // requested output format (mix format is F32)
    u32 periodFrames = 0;    // 0 = backend default
};

// One audio output device bound to a renderer.
class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    virtual Result Start() = 0;
    virtual Result Stop() = 0;
    virtual bool IsRunning() const = 0;
    virtual AudioFormat Format() const = 0;
    virtual const char* Name() const = 0;

    // Null/offline devices: synchronously pull `frameCount` frames into `out` (interleaved
    // f32, device channel count). Returns frames written. Real devices return 0 (they pull
    // on their own thread) - callers use Start()/Stop() for those.
    virtual u32 RenderOffline(f32* out, u32 frameCount) {
        (void)out;
        (void)frameCount;
        return 0;
    }
};

// Creates output devices, hiding the concrete backend. A future revision enumerates devices
// and hot-plug; Phase 1 creates the default device for a backend.
class AudioDeviceManager {
public:
    AudioDeviceManager();
    ~AudioDeviceManager();

    // Creates a device that will pull audio from `renderer`. Returns nullptr on failure.
    std::unique_ptr<IAudioDevice> CreateDevice(const DeviceDesc& desc, IAudioRenderer* renderer);
};

} // namespace reverie
