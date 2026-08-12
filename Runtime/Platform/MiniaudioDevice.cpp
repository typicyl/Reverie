// Reverie/Runtime/Platform/MiniaudioDevice.cpp - see MiniaudioDevice.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// miniaudio is included here as DECLARATIONS only; MINIAUDIO_IMPLEMENTATION lives in the
// single MiniaudioImpl.cpp TU. This file translates Reverie's device abstraction onto a
// ma_device and forwards the audio-thread callback to the engine's IAudioRenderer.
#include "Platform/MiniaudioDevice.h"

#include "Core/Log.h"

#include "miniaudio.h"

namespace reverie {

struct MiniaudioDevice::Impl {
    ma_device device{};
    IAudioRenderer* renderer = nullptr;
    AudioFormat format;
    bool inited = false;
    bool running = false;

    // Static member (not a free function) so it can name the private Impl the ma_device's
    // pUserData points at, and so the config that references it is built inside a member.
    static void DataCallback(ma_device* device, void* output, const void* input,
                             ma_uint32 frameCount);
};

void MiniaudioDevice::Impl::DataCallback(ma_device* device, void* output, const void* /*input*/,
                                         ma_uint32 frameCount) {
    auto* impl = static_cast<Impl*>(device->pUserData);
    if (impl == nullptr || impl->renderer == nullptr) return;
    impl->renderer->RenderAudio(static_cast<f32*>(output), frameCount, impl->format.channels,
                                impl->format.sampleRate);
}

MiniaudioDevice::MiniaudioDevice(const AudioFormat& format, u32 periodFrames,
                                 IAudioRenderer* renderer)
    : impl_(std::make_unique<Impl>()) {
    impl_->renderer = renderer;
    impl_->format = format;
    impl_->format.format = SampleFormat::F32; // Reverie mixes in f32

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = format.channels;
    cfg.sampleRate = format.sampleRate;
    cfg.periodSizeInFrames = periodFrames; // 0 = backend default
    cfg.dataCallback = &Impl::DataCallback;
    cfg.pUserData = impl_.get();

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        LogMessage(LogLevel::Error, "MiniaudioDevice: ma_device_init failed");
        impl_->inited = false;
        return;
    }
    // The device may negotiate a different rate/channel count; reflect what we got.
    impl_->format.channels = impl_->device.playback.channels;
    impl_->format.sampleRate = impl_->device.sampleRate;
    impl_->inited = true;
}

MiniaudioDevice::~MiniaudioDevice() {
    if (impl_ && impl_->inited) {
        if (impl_->running) ma_device_stop(&impl_->device);
        ma_device_uninit(&impl_->device);
    }
}

Result MiniaudioDevice::Start() {
    if (!impl_->inited) return Result::DeviceError;
    if (impl_->running) return Result::Ok;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) return Result::DeviceError;
    impl_->running = true;
    return Result::Ok;
}

Result MiniaudioDevice::Stop() {
    if (!impl_->inited || !impl_->running) return Result::Ok;
    ma_device_stop(&impl_->device);
    impl_->running = false;
    return Result::Ok;
}

bool MiniaudioDevice::IsRunning() const { return impl_ && impl_->running; }

AudioFormat MiniaudioDevice::Format() const { return impl_->format; }

const char* MiniaudioDevice::Name() const { return "miniaudio"; }

} // namespace reverie
