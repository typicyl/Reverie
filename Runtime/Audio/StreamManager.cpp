// Reverie/Runtime/Audio/StreamManager.cpp - see StreamManager.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Audio/StreamManager.h"

#include <chrono>

namespace reverie {

using std::memory_order_acquire;
using std::memory_order_relaxed;
using std::memory_order_release;

StreamManager::~StreamManager() { Stop(); }

void StreamManager::Start(u32 slots, u32 ringFramesPerSlot) {
    if (started_) return;
    slotCount_ = slots == 0 ? 1 : slots;
    ringFrames_ = ringFramesPerSlot < 1024 ? 1024 : ringFramesPerSlot;
    slots_ = std::make_unique<Slot[]>(slotCount_);
    quit_.store(false, memory_order_relaxed);
    worker_ = std::thread([this] { WorkerMain(); });
    started_ = true;
}

void StreamManager::Stop() {
    if (!started_) return;
    quit_.store(true, memory_order_relaxed);
    wakeCv_.notify_all();
    if (worker_.joinable()) worker_.join();
    slots_.reset();
    slotCount_ = 0;
    started_ = false;
}

int StreamManager::Open(const u8* data, usize size, bool loop) {
    if (!started_ || data == nullptr || size == 0) return -1;
    for (u32 n = 0; n < slotCount_; ++n) {
        const u32 i = (searchHint_ + n) % slotCount_;
        Slot& s = slots_[i];
        if (static_cast<SlotState>(s.state.load(memory_order_acquire)) != SlotState::Free) continue;
        // Slot is Free and the worker won't touch it until we publish Active: fill it.
        s.blob.assign(data, data + size);
        if (Failed(s.stream.Open(s.blob.data(), s.blob.size()))) {
            s.blob.clear();
            return -1;
        }
        s.channels = s.stream.Channels();
        s.loop = loop;
        s.ring.Init(static_cast<usize>(ringFrames_) * (s.channels == 0 ? 1 : s.channels));
        s.ring.Clear();
        s.decodeTmp.assign(static_cast<usize>(4096) * (s.channels == 0 ? 1 : s.channels), 0.0f);
        s.releaseReq.store(false, memory_order_relaxed);
        s.producerDone.store(false, memory_order_relaxed);
        s.state.store(static_cast<u8>(SlotState::Active), memory_order_release); // PUBLISH
        searchHint_ = (i + 1) % slotCount_;
        wakeCv_.notify_all();
        return static_cast<int>(i);
    }
    return -1; // pool full
}

void StreamManager::RequestRelease(int slot) {
    if (slot < 0 || static_cast<u32>(slot) >= slotCount_) return;
    slots_[static_cast<u32>(slot)].releaseReq.store(true, memory_order_release);
    wakeCv_.notify_all();
}

u32 StreamManager::SlotChannels(int slot) const {
    if (slot < 0 || static_cast<u32>(slot) >= slotCount_) return 0;
    return slots_[static_cast<u32>(slot)].channels;
}

u32 StreamManager::Read(int slot, f32* out, u32 frames, u32 channels, bool* endedOut) {
    if (endedOut != nullptr) *endedOut = false;
    if (slot < 0 || static_cast<u32>(slot) >= slotCount_ || out == nullptr) return 0;
    Slot& s = slots_[static_cast<u32>(slot)];
    if (static_cast<SlotState>(s.state.load(memory_order_acquire)) != SlotState::Active) return 0;
    const u32 ch = s.channels;
    if (ch == 0) return 0;
    // Pop min(requested, available) whole frames from the ring.
    const usize wantSamples = static_cast<usize>(frames) * ch;
    usize got = s.ring.Read(out, wantSamples);
    const u32 gotFrames = static_cast<u32>(got / ch);
    // If the requested channel count differs from the stream's, only the first min channels are
    // meaningful; callers pass the stream's channel count. Zero-fill any shortfall for safety.
    if (channels == ch) {
        for (usize i = got; i < wantSamples; ++i) out[i] = 0.0f;
    }
    if (endedOut != nullptr)
        *endedOut = s.producerDone.load(memory_order_acquire) && s.ring.ReadAvail() == 0;
    return gotFrames;
}

void StreamManager::WorkerMain() {
    for (;;) {
        bool didWork = false;
        for (u32 i = 0; i < slotCount_; ++i) {
            Slot& s = slots_[i];
            const SlotState st = static_cast<SlotState>(s.state.load(memory_order_acquire));
            if (st == SlotState::Active) {
                if (s.releaseReq.load(memory_order_acquire)) {
                    // Move to Releasing; drop resources, then Free.
                    s.state.store(static_cast<u8>(SlotState::Releasing), memory_order_relaxed);
                }
                // Refill the ring while there is space and data remains.
                const u32 ch = s.channels == 0 ? 1 : s.channels;
                usize spaceSamples = s.ring.WriteAvail();
                while (spaceSamples >= ch) {
                    u32 wantFrames = static_cast<u32>(spaceSamples / ch);
                    if (wantFrames > 4096) wantFrames = 4096;
                    const u32 read = s.stream.Read(s.decodeTmp.data(), wantFrames, s.loop);
                    if (read == 0) {
                        s.producerDone.store(true, memory_order_release); // end (non-loop)
                        break;
                    }
                    s.ring.Write(s.decodeTmp.data(), static_cast<usize>(read) * ch);
                    didWork = true;
                    spaceSamples = s.ring.WriteAvail();
                }
            } else if (st == SlotState::Releasing) {
                s.stream = HdsrfStream{}; // detach from blob
                s.blob.clear();
                s.blob.shrink_to_fit();
                s.channels = 0;
                s.releaseReq.store(false, memory_order_relaxed);
                s.producerDone.store(false, memory_order_relaxed);
                s.state.store(static_cast<u8>(SlotState::Free), memory_order_release);
                didWork = true;
            }
        }
        if (quit_.load(memory_order_relaxed)) return;
        if (!didWork) {
            std::unique_lock<std::mutex> lock(wakeMutex_);
            wakeCv_.wait_for(lock, std::chrono::milliseconds(2));
            if (quit_.load(memory_order_relaxed)) return;
        }
    }
}

} // namespace reverie
