// Reverie/Runtime/Platform/AudioDeviceManager.cpp - device factory (backend selection).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Platform/AudioDevice.h"

#include "Platform/MiniaudioDevice.h"
#include "Platform/NullDevice.h"

namespace reverie {

AudioDeviceManager::AudioDeviceManager() = default;
AudioDeviceManager::~AudioDeviceManager() = default;

std::unique_ptr<IAudioDevice> AudioDeviceManager::CreateDevice(const DeviceDesc& desc,
                                                               IAudioRenderer* renderer) {
    if (renderer == nullptr) return nullptr;

    AudioFormat fmt = desc.format;
    if (!fmt.Valid()) fmt = AudioFormat{}; // stereo / 48k / f32 default
    fmt.format = SampleFormat::F32;        // Reverie mixes in f32

    switch (desc.backend) {
        case DeviceBackend::Null:
            return std::make_unique<NullDevice>(fmt, renderer);
        case DeviceBackend::Miniaudio:
            return std::make_unique<MiniaudioDevice>(fmt, desc.periodFrames, renderer);
    }
    return nullptr;
}

} // namespace reverie
