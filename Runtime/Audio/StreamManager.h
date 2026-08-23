// Reverie/Runtime/Audio/StreamManager.h - background streaming of .HDSRF audio into ring buffers.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Streaming voices decode long assets in the background instead of loading them whole into RAM. A
// StreamManager owns a fixed pool of stream slots; ONE background thread tops up every active slot's
// lock-free SampleRing from its HdsrfStream (bounded memory: only the ring + one decoded chunk per
// slot are ever resident). The audio thread pulls decoded frames from the ring with Read(), never
// blocking and never doing I/O. Lifecycle mirrors the voice pool's atomic state machine:
//
//   Free --(control Open: copy blob, arm)--> Active --(voice teardown RequestRelease)--> Releasing
//        <--(background thread finishes draining + frees the blob)--------------------------------
//
// The blob is COPIED into the slot so it outlives the caller. Open runs on the control thread; Read
// on the audio thread; decode + release on the background thread. state gates cross-thread field
// visibility exactly like Voice::state.
#pragma once

#include "Core/SampleRing.h"
#include "Core/Types.h"
#include "Serialization/Hdsrf.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace reverie {

class StreamManager {
public:
    ~StreamManager();

    void Start(u32 slots, u32 ringFramesPerSlot); // allocate the pool + start the worker
    void Stop();                                  // stop the worker + free everything

    // Control thread: begin streaming a copy of an .HDSRF blob. Returns a slot id (>=0) or -1.
    int Open(const u8* data, usize size, bool loop);
    // Audio thread (voice teardown): ask the worker to drain + free the slot. Safe to call once.
    void RequestRelease(int slot);

    u32 SlotChannels(int slot) const;

    // Audio thread: pull up to `frames` interleaved f32 into `out`. Returns frames read (may be < on
    // an underrun or at end). `endedOut` (optional) is set true when the stream has finished AND the
    // ring is drained (the voice may then end).
    u32 Read(int slot, f32* out, u32 frames, u32 channels, bool* endedOut);

private:
    enum class SlotState : u8 { Free = 0, Active = 1, Releasing = 2 };
    struct Slot {
        std::atomic<u8> state{static_cast<u8>(SlotState::Free)};
        std::atomic<bool> releaseReq{false};
        std::atomic<bool> producerDone{false}; // stream hit end (non-loop): no more will be produced
        std::vector<u8> blob;                   // owned copy (referenced by `stream`)
        HdsrfStream stream;
        SampleRing ring;
        u32 channels = 0;
        bool loop = false;
        std::vector<f32> decodeTmp; // worker scratch
        Slot() = default;
    };

    void WorkerMain();

    std::unique_ptr<Slot[]> slots_;
    u32 slotCount_ = 0;
    u32 ringFrames_ = 0;
    std::thread worker_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    std::atomic<bool> quit_{false};
    bool started_ = false;
    u32 searchHint_ = 0; // control-thread free-slot scan cursor
};

} // namespace reverie
