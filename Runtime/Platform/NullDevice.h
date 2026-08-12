// Reverie/Runtime/Platform/NullDevice.h - headless output backend.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Touches no hardware. Used for tests/CI, dedicated servers, and (later) the offline file
// renderer. The host drives it synchronously via RenderOffline; Start/Stop only flip a flag.
#pragma once

#include "Platform/AudioDevice.h"

namespace reverie {

class NullDevice final : public IAudioDevice {
public:
    NullDevice(const AudioFormat& format, IAudioRenderer* renderer);

    Result Start() override;
    Result Stop() override;
    bool IsRunning() const override;
    AudioFormat Format() const override;
    const char* Name() const override;
    u32 RenderOffline(f32* out, u32 frameCount) override;

private:
    AudioFormat format_;
    IAudioRenderer* renderer_;
    bool running_ = false;
};

} // namespace reverie
