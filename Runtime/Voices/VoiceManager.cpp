// Reverie/Runtime/Voices/VoiceManager.cpp - see VoiceManager.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Voices/VoiceManager.h"

#include "Audio/StreamManager.h"
#include "Mixer/Mixer.h"
#include "Parameters/ParameterStore.h"

#include <algorithm>

namespace reverie {

using std::memory_order_acquire;
using std::memory_order_relaxed;
using std::memory_order_release;

namespace {
inline VoiceState LoadState(const Voice& v, std::memory_order o) {
    return static_cast<VoiceState>(v.state.load(o));
}
// Smoothstep in [lo,hi]; degenerate range -> hard step at lo.
inline f32 Smoothstep(f32 lo, f32 hi, f32 x) {
    if (hi <= lo) return x >= lo ? 1.0f : 0.0f;
    f32 t = (x - lo) / (hi - lo);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

void VoiceManager::Reserve(u32 capacity) {
    if (capacity < 1) capacity = 1;
    if (capacity <= capacity_) return; // never shrink
    // Allocate a fresh pool. Reserve is an init-time operation (no concurrent playback), so simply
    // moving the still-live voices would be wrong anyway; callers Reserve once before use.
    voices_ = std::make_unique<Voice[]>(capacity);
    capacity_ = capacity;
    order_.reserve(capacity);
    searchHint_ = 0;
}

VoiceId VoiceManager::AllocId() {
    VoiceId id = nextId_.fetch_add(1, memory_order_relaxed);
    if (id == kInvalidId) id = nextId_.fetch_add(1, memory_order_relaxed); // skip 0 on wrap
    return id;
}

void VoiceManager::SetMaxRealVoices(u32 count) {
    maxReal_.store(count == 0 ? 1 : count, memory_order_relaxed);
    ReprioritizeControl();
}

u32 VoiceManager::MaxRealVoices() const { return maxReal_.load(memory_order_relaxed); }

int VoiceManager::ClaimFreeSlotControl() {
    // Single control thread: a slot observed Free stays Free until we fill it (the audio thread
    // only ever turns slots Free, never claims them), so no CAS is needed to claim.
    for (u32 n = 0; n < capacity_; ++n) {
        const u32 i = (searchHint_ + n) % capacity_;
        if (LoadState(voices_[i], memory_order_acquire) == VoiceState::Free) {
            searchHint_ = (i + 1) % capacity_;
            return static_cast<int>(i);
        }
    }
    return -1; // pool full
}

VoiceId VoiceManager::Play(const VoiceSpawn& spawn) {
    if (capacity_ == 0) return kInvalidId;
    // Streaming voices carry no in-memory buffer (they pull from a StreamManager slot).
    if (spawn.streamSlot < 0 && (!spawn.buffer || spawn.buffer->Empty())) return kInvalidId;
    const int slot = ClaimFreeSlotControl();
    if (slot < 0) return kInvalidId; // pool full: drop (a hard ceiling, not unbounded growth)
    Voice& v = voices_[static_cast<u32>(slot)];

    // The slot is Free and invisible to the audio thread: fill it before publishing Playing.
    // Assigning buffer here also drops any buffer this slot retained from a previous voice - on the
    // control thread, so no large heap free happens on the audio path.
    v.buffer = spawn.buffer;
    v.cursor = 0.0;
    v.volume = spawn.volume < 0.0f ? 0.0f : spawn.volume;
    v.pitch = spawn.pitch < 0.01f ? 0.01f : spawn.pitch;
    v.loop = spawn.loop;
    v.id = AllocId();
    v.bus = spawn.bus;
    v.priority = spawn.priority;
    v.eventInstance = spawn.eventInstance;
    v.concurrencyGroup = spawn.concurrencyGroup;
    // Record the INTENT to spatialize; the source slot is acquired lazily on the AUDIO thread (in
    // MixToBuses) so that all spatial-renderer access is single-threaded. Acquiring here (control
    // thread) would race the audio thread's SetSource/SubmitSourceAudio/Render on the shared source
    // pool. Until a slot is acquired (or if the source pool is full), the voice mixes 2D to v.bus.
    v.spatial = spawn.spatial && spatial_ != nullptr;
    v.spatialSlot = -1;
    v.quality = spawn.quality;
    v.position = spawn.position;
    v.minDistance = spawn.minDistance;
    v.maxDistance = spawn.maxDistance;
    v.volumeParam = spawn.volumeParam;
    v.paramLo = spawn.paramLo;
    v.paramHi = spawn.paramHi;
    v.streamSlot = spawn.streamSlot;
    v.age = nextAge_.fetch_add(1, memory_order_relaxed);
    v.virtualizedFlag.store(0, memory_order_relaxed); // ReprioritizeControl sets the real value
    v.state.store(static_cast<u8>(VoiceState::Playing), memory_order_release); // PUBLISH

    ReprioritizeControl();
    return v.id;
}

void VoiceManager::Stop(VoiceId id) {
    if (id == kInvalidId || capacity_ == 0) return;
    for (u32 i = 0; i < capacity_; ++i) {
        Voice& v = voices_[i];
        if (LoadState(v, memory_order_acquire) == VoiceState::Playing && v.id == id) {
            u8 expected = static_cast<u8>(VoiceState::Playing);
            v.state.compare_exchange_strong(expected, static_cast<u8>(VoiceState::Stopping),
                                            std::memory_order_acq_rel, memory_order_relaxed);
            break; // ids are unique among Playing voices
        }
    }
    ReprioritizeControl();
}

void VoiceManager::StopInstance(InstanceId instance) {
    if (instance == 0 || capacity_ == 0) return;
    for (u32 i = 0; i < capacity_; ++i) {
        Voice& v = voices_[i];
        if (LoadState(v, memory_order_acquire) == VoiceState::Playing && v.eventInstance == instance) {
            u8 expected = static_cast<u8>(VoiceState::Playing);
            v.state.compare_exchange_strong(expected, static_cast<u8>(VoiceState::Stopping),
                                            std::memory_order_acq_rel, memory_order_relaxed);
        }
    }
    ReprioritizeControl();
}

void VoiceManager::StopGroup(u32 group) {
    if (group == 0 || capacity_ == 0) return;
    for (u32 i = 0; i < capacity_; ++i) {
        Voice& v = voices_[i];
        if (LoadState(v, memory_order_acquire) == VoiceState::Playing && v.concurrencyGroup == group) {
            u8 expected = static_cast<u8>(VoiceState::Playing);
            v.state.compare_exchange_strong(expected, static_cast<u8>(VoiceState::Stopping),
                                            std::memory_order_acq_rel, memory_order_relaxed);
        }
    }
    ReprioritizeControl();
}

void VoiceManager::StopAll() {
    // Playback-safe: request stop on everything; the audio thread completes teardown. (For shutdown,
    // where the device is already stopped, use ReleaseAllForShutdown instead.)
    if (capacity_ == 0) return;
    for (u32 i = 0; i < capacity_; ++i) {
        Voice& v = voices_[i];
        u8 expected = static_cast<u8>(VoiceState::Playing);
        v.state.compare_exchange_strong(expected, static_cast<u8>(VoiceState::Stopping),
                                        std::memory_order_acq_rel, memory_order_relaxed);
    }
    ReprioritizeControl();
}

void VoiceManager::ReleaseAllForShutdown() {
    // Caller guarantees the audio device is stopped -> no concurrent MixToBuses -> the control
    // thread may release spatial sources and drop buffers directly.
    if (capacity_ == 0) return;
    for (u32 i = 0; i < capacity_; ++i) {
        Voice& v = voices_[i];
        if (LoadState(v, memory_order_relaxed) != VoiceState::Free) {
            if (v.spatialSlot >= 0 && spatial_ != nullptr) spatial_->ReleaseSource(v.spatialSlot);
            v.spatialSlot = -1;
            v.spatial = false;
            if (v.streamSlot >= 0 && streams_ != nullptr) streams_->RequestRelease(v.streamSlot);
            v.streamSlot = -1;
            v.buffer.reset();
            v.state.store(static_cast<u8>(VoiceState::Free), memory_order_relaxed);
        }
    }
}

void VoiceManager::SetVoicePosition(VoiceId id, const Float3& position) {
    if (id == kInvalidId || capacity_ == 0) return;
    for (u32 i = 0; i < capacity_; ++i) {
        Voice& v = voices_[i];
        if (LoadState(v, memory_order_acquire) == VoiceState::Playing && v.id == id && v.spatial) {
            v.position = position; // benign torn read on the audio side (at most one block stale)
            break;
        }
    }
}

u32 VoiceManager::ActiveVoiceCount() const {
    u32 n = 0;
    for (u32 i = 0; i < capacity_; ++i)
        if (LoadState(voices_[i], memory_order_acquire) == VoiceState::Playing) ++n;
    return n;
}

u32 VoiceManager::RealVoiceCount() const {
    u32 n = 0;
    for (u32 i = 0; i < capacity_; ++i) {
        const Voice& v = voices_[i];
        if (LoadState(v, memory_order_acquire) == VoiceState::Playing &&
            v.virtualizedFlag.load(memory_order_relaxed) == 0)
            ++n;
    }
    return n;
}

u32 VoiceManager::VirtualVoiceCount() const {
    u32 n = 0;
    for (u32 i = 0; i < capacity_; ++i) {
        const Voice& v = voices_[i];
        if (LoadState(v, memory_order_acquire) == VoiceState::Playing &&
            v.virtualizedFlag.load(memory_order_relaxed) != 0)
            ++n;
    }
    return n;
}

u32 VoiceManager::GroupVoiceCount(u32 group) const {
    u32 n = 0;
    for (u32 i = 0; i < capacity_; ++i) {
        const Voice& v = voices_[i];
        if (LoadState(v, memory_order_acquire) == VoiceState::Playing && v.concurrencyGroup == group)
            ++n;
    }
    return n;
}

u32 VoiceManager::InstanceVoiceCount(InstanceId instance) const {
    u32 n = 0;
    for (u32 i = 0; i < capacity_; ++i) {
        const Voice& v = voices_[i];
        if (LoadState(v, memory_order_acquire) == VoiceState::Playing && v.eventInstance == instance)
            ++n;
    }
    return n;
}

void VoiceManager::ReprioritizeControl() {
    // Control-thread only. Order playing voices by priority (desc), then age (asc); the top
    // `maxReal_` are real, the rest virtual. Writes each voice's virtualizedFlag (atomic, read by
    // the audio thread). order_ is reserved to capacity_ so this never allocates.
    //
    // NOTE: this runs on Play/Stop*/SetMaxRealVoices, NOT on the audio thread. So when a REAL voice
    // finishes naturally on the audio thread, a virtual voice is not promoted into the freed budget
    // slot until the next control op. In practice control ops happen every frame, so the split
    // self-heals promptly; games never notice. (Matches the previous design, which also only
    // reprioritized on control operations.)
    order_.clear();
    for (u32 i = 0; i < capacity_; ++i)
        if (LoadState(voices_[i], memory_order_acquire) == VoiceState::Playing) order_.push_back(i);

    std::sort(order_.begin(), order_.end(), [this](u32 a, u32 b) {
        if (voices_[a].priority != voices_[b].priority)
            return voices_[a].priority > voices_[b].priority;
        return voices_[a].age < voices_[b].age;
    });

    const u32 maxReal = maxReal_.load(memory_order_relaxed);
    for (usize rank = 0; rank < order_.size(); ++rank)
        voices_[order_[rank]].virtualizedFlag.store(rank >= maxReal ? 1u : 0u, memory_order_relaxed);
}

void VoiceManager::TeardownAudio(Voice& v) {
    // Audio-thread only. Release the spatial source (safe here - same thread as the renderer's
    // Render) and publish Free. The buffer's shared_ptr is intentionally LEFT in the slot; it is
    // dropped on the control thread when the slot is next reused, so no heap free runs here.
    if (v.spatialSlot >= 0 && spatial_ != nullptr) spatial_->ReleaseSource(v.spatialSlot);
    v.spatialSlot = -1;
    v.spatial = false;
    if (v.streamSlot >= 0 && streams_ != nullptr) streams_->RequestRelease(v.streamSlot);
    v.streamSlot = -1;
    v.state.store(static_cast<u8>(VoiceState::Free), memory_order_release);
}

void VoiceManager::MixToBuses(Mixer& mixer, u32 frameCount, u32 channels, u32 dstSampleRate) {
    if (frameCount == 0 || channels == 0 || dstSampleRate == 0 || capacity_ == 0) return;

    for (u32 i = 0; i < capacity_; ++i) {
        Voice& v = voices_[i];
        const VoiceState st = LoadState(v, memory_order_acquire);
        if (st == VoiceState::Free) continue;
        if (st == VoiceState::Stopping) { // control requested a stop
            TeardownAudio(v);
            continue;
        }
        // Playing. Effective gain (base * optional parameter modulation) applies to every path.
        const bool virtualized = v.virtualizedFlag.load(memory_order_relaxed) != 0;
        f32 effVol = v.volume;
        if (v.volumeParam != kInvalidId && params_ != nullptr)
            effVol *= Smoothstep(v.paramLo, v.paramHi, params_->Value(v.volumeParam));

        // Streaming voice: pull decoded frames from the StreamManager ring (non-spatial, 2D). The
        // background thread keeps the ring fed; the audio thread only pops (lock-free). When the
        // stream has ended and the ring is drained, the voice finishes.
        if (v.streamSlot >= 0 && streams_ != nullptr) {
            u32 sch = streams_->SlotChannels(v.streamSlot);
            if (sch == 0) sch = 1;
            const usize need = static_cast<usize>(frameCount) * sch;
            if (streamTmp_.size() < need) streamTmp_.assign(need, 0.0f);
            bool ended = false;
            const u32 got = streams_->Read(v.streamSlot, streamTmp_.data(), frameCount, sch, &ended);
            if (!virtualized) {
                if (f32* sout = mixer.BusBuffer(v.bus)) {
                    for (u32 f = 0; f < got; ++f) {
                        f32 l, r;
                        if (sch == 1) {
                            l = r = streamTmp_[f] * effVol;
                        } else {
                            l = streamTmp_[static_cast<usize>(f) * sch + 0] * effVol;
                            r = streamTmp_[static_cast<usize>(f) * sch + 1] * effVol;
                        }
                        sout[static_cast<usize>(f) * channels + 0] += l;
                        if (channels >= 2) sout[static_cast<usize>(f) * channels + 1] += r;
                    }
                }
            }
            if (ended) TeardownAudio(v);
            continue;
        }

        if (!v.buffer) {
            TeardownAudio(v);
            continue;
        }
        const AudioBuffer& b = *v.buffer;
        const u32 srcCh = b.channels;
        const u32 srcFrames = b.FrameCount();
        if (srcCh == 0 || srcFrames == 0) {
            TeardownAudio(v);
            continue;
        }

        // Lazily acquire the spatial source slot HERE (audio thread) - never on the control thread -
        // so the renderer's source pool is only ever touched by one thread. Only for voices actually
        // about to render (not virtual), so silent/virtual voices don't hold scarce source slots; a
        // voice that couldn't get a slot yet (pool full) retries next block and mixes 2D meanwhile.
        if (!virtualized && v.spatial && v.spatialSlot < 0 && spatial_ != nullptr)
            v.spatialSlot = spatial_->AcquireSource(v.quality);
        const bool spatialVoice = v.spatial && v.spatialSlot >= 0 && spatial_ != nullptr;
        f32* out = nullptr;
        f32* mono = nullptr;
        if (!virtualized) {
            if (spatialVoice) {
                if (monoTmp_.size() < frameCount)
                    monoTmp_.assign(frameCount, 0.0f);
                else
                    std::fill(monoTmp_.begin(), monoTmp_.begin() + frameCount, 0.0f);
                mono = monoTmp_.data();
                spatial_->SetSource(v.spatialSlot, v.position, effVol, v.minDistance, v.maxDistance);
            } else {
                out = mixer.BusBuffer(v.bus);
            }
        }
        const bool audible = out != nullptr || mono != nullptr;
        const f64 step =
            (static_cast<f64>(b.sampleRate) / static_cast<f64>(dstSampleRate)) * v.pitch;

        bool finished = false;
        for (u32 f = 0; f < frameCount; ++f) {
            if (v.cursor >= static_cast<f64>(srcFrames)) {
                if (v.loop) {
                    while (v.cursor >= static_cast<f64>(srcFrames))
                        v.cursor -= static_cast<f64>(srcFrames);
                } else {
                    finished = true;
                    break;
                }
            }
            if (audible) {
                const u32 i0 = static_cast<u32>(v.cursor);
                const u32 i1 = (i0 + 1 < srcFrames) ? i0 + 1 : (v.loop ? 0u : i0);
                const f32 frac = static_cast<f32>(v.cursor - static_cast<f64>(i0));
                if (mono != nullptr) {
                    // Downmix to mono WITHOUT volume - the spatial renderer applies volume,
                    // distance and panning from the source's params.
                    f32 m;
                    if (srcCh == 1) {
                        m = b.samples[i0] + (b.samples[i1] - b.samples[i0]) * frac;
                    } else {
                        const f32 l0 = b.samples[static_cast<usize>(i0) * srcCh + 0];
                        const f32 l1 = b.samples[static_cast<usize>(i1) * srcCh + 0];
                        const f32 r0 = b.samples[static_cast<usize>(i0) * srcCh + 1];
                        const f32 r1 = b.samples[static_cast<usize>(i1) * srcCh + 1];
                        m = 0.5f * ((l0 + (l1 - l0) * frac) + (r0 + (r1 - r0) * frac));
                    }
                    mono[f] += m;
                } else {
                    f32 sl, sr;
                    if (srcCh == 1) {
                        const f32 s =
                            (b.samples[i0] + (b.samples[i1] - b.samples[i0]) * frac) * effVol;
                        sl = s;
                        sr = s;
                    } else {
                        const f32 l0 = b.samples[static_cast<usize>(i0) * srcCh + 0];
                        const f32 l1 = b.samples[static_cast<usize>(i1) * srcCh + 0];
                        const f32 r0 = b.samples[static_cast<usize>(i0) * srcCh + 1];
                        const f32 r1 = b.samples[static_cast<usize>(i1) * srcCh + 1];
                        sl = (l0 + (l1 - l0) * frac) * effVol;
                        sr = (r0 + (r1 - r0) * frac) * effVol;
                    }
                    out[static_cast<usize>(f) * channels + 0] += sl;
                    if (channels >= 2) out[static_cast<usize>(f) * channels + 1] += sr;
                }
            }
            v.cursor += step;
        }
        if (mono != nullptr) spatial_->SubmitSourceAudio(v.spatialSlot, mono, frameCount);
        if (finished) TeardownAudio(v);
    }
}

} // namespace reverie
