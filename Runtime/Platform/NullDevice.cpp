// Reverie/Runtime/Platform/NullDevice.cpp - see NullDevice.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Platform/NullDevice.h"

namespace reverie {

NullDevice::NullDevice(const AudioFormat& format, IAudioRenderer* renderer)
    : format_(format), renderer_(renderer) {}

Result NullDevice::Start() {
    running_ = true;
    return Result::Ok;
}

Result NullDevice::Stop() {
    running_ = false;
    return Result::Ok;
}

bool NullDevice::IsRunning() const { return running_; }

AudioFormat NullDevice::Format() const { return format_; }

const char* NullDevice::Name() const { return "Null (headless)"; }

u32 NullDevice::RenderOffline(f32* out, u32 frameCount) {
    // Offline pull is independent of Start/Stop - it exists precisely so tests/CI and the
    // offline renderer can pump the graph without a running device.
    if (renderer_ == nullptr || out == nullptr || frameCount == 0) return 0;
    renderer_->RenderAudio(out, frameCount, format_.channels, format_.sampleRate);
    return frameCount;
}

} // namespace reverie
