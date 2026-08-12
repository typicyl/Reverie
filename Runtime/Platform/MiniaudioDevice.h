// Reverie/Runtime/Platform/MiniaudioDevice.h - real playback backend (miniaudio).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The default output device. miniaudio is a private implementation detail: no ma_* type
// appears in this header (pimpl), so nothing outside MiniaudioDevice.cpp depends on it and
// the backend can be swapped without changing the runtime or SDK.
#pragma once

#include "Platform/AudioDevice.h"

#include <memory>

namespace reverie {

class MiniaudioDevice final : public IAudioDevice {
public:
    MiniaudioDevice(const AudioFormat& format, u32 periodFrames, IAudioRenderer* renderer);
    ~MiniaudioDevice() override;

    Result Start() override;
    Result Stop() override;
    bool IsRunning() const override;
    AudioFormat Format() const override;
    const char* Name() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reverie
