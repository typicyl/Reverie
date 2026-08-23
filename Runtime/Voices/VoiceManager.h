// Reverie/Runtime/Voices/VoiceManager.h - the lock-free voice pool + mixer front-end.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Owns every voice in a FIXED, pre-allocated pool and does the actual mixing. Designed for large
// scenes: a budget caps how many voices are REAL (mixed) at once; the rest are VIRTUALIZED - they
// keep advancing their playback position so they stay in sync, but produce no audio. Priority (with
// age as a tie-break) decides who is real.
//
// Threading model (no lock on the audio path):
//   * Control thread: Play/Stop*/SetVoicePosition/SetMaxRealVoices and the count queries. Play
//     claims a Free slot, fills it, and publishes Playing; Stop* flip Playing->Stopping.
//   * Audio thread: MixToBuses drains no lock - it iterates the pool, mixes Playing voices, and
//     completes teardown (spatial release + publishing Free) for Stopping/finished voices.
//   * `Voice::state` (atomic) is the only synchronization point; see Voice.h for the full contract.
// This replaces the earlier single-mutex design; the audio callback never blocks on the control
// thread, never allocates, never sorts per block, and never frees a buffer.
#pragma once

#include "Audio/AudioBuffer.h"
#include "Core/Types.h"
#include "Spatial/SpatialRenderer.h"
#include "Voices/Voice.h"

#include <atomic>
#include <memory>
#include <vector>

namespace reverie {

class Mixer;          // voices mix into their target bus's buffer (Mixer::BusBuffer)
class ParameterStore; // optional: modulates voice volume by a parameter (RTPC)
class StreamManager;  // optional: streaming voices pull decoded frames from here

struct VoiceSpawn {
    std::shared_ptr<const AudioBuffer> buffer;
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    bool loop = false;
    i32 priority = 0;
    InstanceId eventInstance = 0;
    u32 concurrencyGroup = 0;
    BusId bus = kInvalidId; // routing target (kInvalidId = Master)

    // Spatial (3D): when `spatial`, the voice is rendered by the spatial renderer at `position`
    // instead of the flat upmix, and its output goes to the mixer's Spatial bus.
    bool spatial = false;
    Float3 position;
    SpatialQuality quality = SpatialQuality::Panning;
    f32 minDistance = 1.0f;
    f32 maxDistance = 100.0f;

    // Optional volume modulation by a parameter: effective gain = volume * smoothstep(lo,hi,param).
    ParameterId volumeParam = kInvalidId;
    f32 paramLo = 0.0f;
    f32 paramHi = 1.0f;

    // Streaming voice: pulls from a StreamManager slot instead of a buffer (non-spatial). -1 = none.
    int streamSlot = -1;
};

class VoiceManager {
public:
    VoiceManager() = default;
    ~VoiceManager() = default;
    VoiceManager(const VoiceManager&) = delete;
    VoiceManager& operator=(const VoiceManager&) = delete;

    // Optional spatial renderer. When set, spatial voices claim a source slot from it, submit their
    // mono signal each block, and its stereo mix is added to the mixer's Spatial bus. Set on the
    // control thread before playback (or nullptr during teardown).
    void SetSpatialRenderer(ISpatialRenderer* renderer) { spatial_ = renderer; }
    // Optional parameter store for per-voice volume modulation (read on the audio thread).
    void SetParameterStore(const ParameterStore* params) { params_ = params; }
    // Optional stream manager for streaming voices (Read on the audio thread, release on teardown).
    void SetStreamManager(StreamManager* streams) { streams_ = streams; }

    // Allocates the fixed voice pool. Call once at engine init before any Play (grows if called
    // again with a larger capacity; never shrinks). Not thread-safe with playback.
    //
    // Capacity is a hard ceiling: Play returns kInvalidId when the pool is full (bounded memory, no
    // audio-thread allocation - the point of the redesign). A stopped voice occupies its slot until
    // the audio thread completes teardown on its next block, so a caller that Plays/Stops in a tight
    // loop WITHOUT ever rendering can exhaust the pool; on a live device every block reclaims, so
    // this only affects headless/paused callers. Size the pool above the real-voice budget so
    // over-budget voices virtualize (they still occupy a slot) rather than being dropped.
    void Reserve(u32 capacity);

    void SetMaxRealVoices(u32 count); // 0 is treated as 1 (never zero the budget)
    u32 MaxRealVoices() const;

    // Updates a live spatial voice's world position (no-op if the voice is gone / not spatial).
    void SetVoicePosition(VoiceId id, const Float3& position);

    VoiceId Play(const VoiceSpawn& spawn); // kInvalidId if buffer is empty or the pool is full
    void Stop(VoiceId id);
    void StopInstance(InstanceId instance);
    void StopGroup(u32 group);
    void StopAll(); // playback-safe: requests stop on all voices (audio thread tears them down)

    // Synchronous teardown for shutdown ONLY: the caller guarantees the audio device is already
    // stopped, so spatial slots can be released and buffers dropped on the control thread.
    void ReleaseAllForShutdown();

    u32 ActiveVoiceCount() const;  // total playing (real + virtual)
    u32 RealVoiceCount() const;    // currently mixed
    u32 VirtualVoiceCount() const; // currently virtual
    u32 GroupVoiceCount(u32 group) const;
    u32 InstanceVoiceCount(InstanceId instance) const;

    // Mixes each real voice into its TARGET BUS's block buffer (via Mixer::BusBuffer), advancing
    // every voice (virtual ones silently), completing teardown for stopping/finished voices. The
    // mixer must be inside a BeginBlock/EndBlock pair sized to `frameCount`/`channels`.
    void MixToBuses(Mixer& mixer, u32 frameCount, u32 channels, u32 dstSampleRate);

private:
    void ReprioritizeControl();       // control-thread: recompute the real/virtual split
    int ClaimFreeSlotControl();       // control-thread: index of a claimed Free slot, or -1 if full
    void TeardownAudio(Voice& v);     // audio-thread: release spatial source, publish Free
    VoiceId AllocId();                // control-thread: next non-zero voice id

    ISpatialRenderer* spatial_ = nullptr;
    const ParameterStore* params_ = nullptr; // optional volume modulation source (audio-thread read)
    StreamManager* streams_ = nullptr;       // optional streaming source
    std::unique_ptr<Voice[]> voices_; // fixed pool; never reallocated after Reserve
    std::vector<f32> streamTmp_;      // audio-thread scratch for streaming voice reads
    u32 capacity_ = 0;
    std::atomic<u32> maxReal_{64};
    std::atomic<VoiceId> nextId_{1};
    std::atomic<u64> nextAge_{1};
    std::vector<f32> monoTmp_;  // audio-thread scratch for downmixing a spatial voice to mono
    std::vector<u32> order_;    // control-thread reprioritize scratch (slot indices)
    u32 searchHint_ = 0;        // control-thread: where to start scanning for a Free slot
};

} // namespace reverie
