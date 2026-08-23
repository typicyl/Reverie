// Reverie/Runtime/Mixer/Mixer.cpp - see Mixer.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Mixer/Mixer.h"

#include "DSP/BiquadFilter.h"
#include "DSP/DelayEffect.h"
#include "DSP/DynamicsEffect.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace reverie {

Mixer::Mixer() {
    // Reserve stable storage up front so buses_ never reallocates and the maps never rehash while
    // the audio thread is reading them (see kMaxBuses rationale in the header).
    buses_.reserve(kMaxBuses);
    index_.reserve(kMaxBuses * 2);
    byName_.reserve(kMaxBuses * 2);
    effectRegistry_.reserve(kMaxBuses * kMaxEffectsPerBus);

    Bus master;
    master.id = nextId_++;
    master.name = "Master";
    master.parent = kInvalidId;
    master.sends.reserve(kMaxSendsPerBus);
    master.effects.reserve(kMaxEffectsPerBus);
    master_ = master.id;
    index_[master.id] = buses_.size();
    byName_[master.name] = master.id;
    buses_.push_back(std::move(master));
}

void Mixer::ConfigureDefault() {
    const char* names[] = {"Music", "SFX", "Dialogue", "Ambience", "UI", "Spatial"};
    for (const char* n : names)
        if (FindBus(n) == kInvalidId) CreateBus(n, master_);
}

Mixer::Bus* Mixer::Get(BusId id) {
    auto it = index_.find(id);
    return it != index_.end() ? &buses_[it->second] : nullptr;
}
const Mixer::Bus* Mixer::Get(BusId id) const {
    auto it = index_.find(id);
    return it != index_.end() ? &buses_[it->second] : nullptr;
}

BusId Mixer::CreateBus(const char* name, BusId parent) {
    if (name == nullptr) return kInvalidId;
    if (BusId existing = FindBus(name); existing != kInvalidId) return existing;
    if (buses_.size() >= kMaxBuses) return kInvalidId; // never reallocate past the reserved storage
    Bus b;
    b.id = nextId_++;
    b.name = name;
    b.parent = (parent != kInvalidId && Get(parent) != nullptr) ? parent : master_;
    b.sends.reserve(kMaxSendsPerBus);
    b.effects.reserve(kMaxEffectsPerBus);
    index_[b.id] = buses_.size();
    byName_[b.name] = b.id;
    buses_.push_back(std::move(b));
    MarkTopologyDirty();
    return buses_.back().id;
}

BusId Mixer::FindBus(const char* name) const {
    if (name == nullptr) return kInvalidId;
    auto it = byName_.find(name);
    return it != byName_.end() ? it->second : kInvalidId;
}

void Mixer::SetBusVolume(BusId bus, f32 volume) {
    if (Bus* b = Get(bus)) b->gain = volume < 0.0f ? 0.0f : volume;
}
f32 Mixer::BusVolume(BusId bus) const {
    const Bus* b = Get(bus);
    return b ? b->gain : 0.0f;
}
void Mixer::SetBusMuted(BusId bus, bool muted) {
    if (Bus* b = Get(bus)) b->muted = muted;
}
bool Mixer::BusMuted(BusId bus) const {
    const Bus* b = Get(bus);
    return b && b->muted;
}
void Mixer::SetBusSoloed(BusId bus, bool soloed) {
    if (Bus* b = Get(bus)) b->soloed = soloed;
}
bool Mixer::BusSoloed(BusId bus) const {
    const Bus* b = Get(bus);
    return b && b->soloed;
}

void Mixer::AddSend(BusId from, BusId to, f32 level) {
    Bus* b = Get(from);
    if (b == nullptr || Get(to) == nullptr || from == to) return;
    if (b->sends.size() >= kMaxSendsPerBus) return; // stay within the reserved (never-realloc) sends
    b->sends.push_back(Send{to, level});
    MarkTopologyDirty();
}

void Mixer::SetDuck(BusId ducked, BusId sidechain, f32 threshold, f32 amount, f32 attackMs,
                    f32 releaseMs) {
    Bus* b = Get(ducked);
    if (b == nullptr || Get(sidechain) == nullptr || ducked == sidechain) return;
    b->hasDuck = true;
    b->duckSidechain = sidechain;
    b->duckThreshold = threshold;
    b->duckAmount = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
    b->duckAttackMs = attackMs;
    b->duckReleaseMs = releaseMs;
    MarkTopologyDirty();
}
void Mixer::ClearDuck(BusId ducked) {
    if (Bus* b = Get(ducked)) {
        b->hasDuck = false;
        b->duckGain = 1.0f;
    }
    MarkTopologyDirty();
}

EffectId Mixer::AddEffect(BusId bus, EffectType type, u32 sampleRate, u32 channels) {
    Bus* b = Get(bus);
    if (b == nullptr) return kInvalidId;
    if (b->effects.size() >= kMaxEffectsPerBus) return kInvalidId; // reserved: no realloc past cap
    std::unique_ptr<IAudioEffect> fx;
    switch (type) {
    case EffectType::Filter: fx = std::make_unique<BiquadFilter>(); break;
    case EffectType::Compressor: fx = std::make_unique<DynamicsEffect>(); break;
    case EffectType::Delay: fx = std::make_unique<DelayEffect>(); break;
    }
    if (!fx) return kInvalidId;
    fx->Prepare(sampleRate, channels);
    IAudioEffect* raw = fx.get(); // stable: the pointee never moves even if the vector reallocs
    b->effects.push_back(std::move(fx));
    effectRegistry_.push_back(raw);
    return static_cast<EffectId>(effectRegistry_.size()); // id = index + 1
}

void Mixer::SetEffectParam(EffectId effect, u32 index, f32 value) {
    if (effect == kInvalidId || effect > effectRegistry_.size()) return;
    effectRegistry_[effect - 1]->SetParam(index, value);
}

f32 Mixer::EffectParam(EffectId effect, u32 index) const {
    if (effect == kInvalidId || effect > effectRegistry_.size()) return 0.0f;
    return effectRegistry_[effect - 1]->GetParam(index);
}

u32 Mixer::BusEffectCount(BusId bus) const {
    const Bus* b = Get(bus);
    return b ? static_cast<u32>(b->effects.size()) : 0u;
}

bool Mixer::DescribeBusAt(u32 index, BusDescriptor& out) const {
    if (index >= buses_.size()) return false;
    const Bus& b = buses_[index];
    out = BusDescriptor{};
    out.name = b.name;
    out.parentName = (b.id != master_ && b.parent != kInvalidId) ? [&] {
        const Bus* p = Get(b.parent);
        return p != nullptr ? p->name : std::string();
    }() : std::string();
    out.gain = b.gain;
    out.muted = b.muted;
    out.soloed = b.soloed;
    out.sends.reserve(b.sends.size());
    for (const Send& s : b.sends) {
        const Bus* d = Get(s.dest);
        if (d != nullptr) out.sends.emplace_back(d->name, s.level);
    }
    return true;
}

f32 Mixer::Meter(BusId bus) const {
    const Bus* b = Get(bus);
    return b ? b->meterPeak : 0.0f;
}

void Mixer::CaptureSnapshot(const char* name) {
    if (name == nullptr) return;
    auto& snap = snapshots_[name];
    snap.clear();
    for (const Bus& b : buses_) snap[b.id] = SnapEntry{b.gain, b.muted};
}
bool Mixer::ApplySnapshot(const char* name) {
    if (name == nullptr) return false;
    auto it = snapshots_.find(name);
    if (it == snapshots_.end()) return false;
    for (const auto& [id, entry] : it->second) {
        if (Bus* b = Get(id)) {
            b->gain = entry.volume;
            b->muted = entry.muted;
        }
    }
    return true;
}

void Mixer::RebuildTopology() {
    const usize n = buses_.size();
    std::vector<std::vector<usize>> adj(n); // u -> v: u must be processed before v
    std::vector<u32> indeg(n, 0);
    auto idx = [this](BusId id) -> usize {
        auto it = index_.find(id);
        return it != index_.end() ? it->second : static_cast<usize>(-1);
    };
    auto edge = [&](usize u, usize v) {
        if (u < n && v < n) {
            adj[u].push_back(v);
            ++indeg[v];
        }
    };
    for (usize i = 0; i < n; ++i) {
        const Bus& b = buses_[i];
        if (b.id != master_ && b.parent != kInvalidId) edge(i, idx(b.parent)); // child -> parent
        for (const Send& s : b.sends) edge(i, idx(s.dest));                     // source -> dest
        if (b.hasDuck) edge(idx(b.duckSidechain), i);                           // sidechain -> ducked
    }
    // Kahn's algorithm.
    topoOrder_.clear();
    topoOrder_.reserve(n);
    std::vector<usize> ready;
    for (usize i = 0; i < n; ++i)
        if (indeg[i] == 0) ready.push_back(i);
    while (!ready.empty()) {
        const usize u = ready.back();
        ready.pop_back();
        topoOrder_.push_back(u);
        for (usize v : adj[u])
            if (--indeg[v] == 0) ready.push_back(v);
    }
    // A cycle (unusual: a send/duck loop) leaves nodes unprocessed; append them so audio still
    // flows, just without a guaranteed ordering for the offending buses.
    if (topoOrder_.size() < n) {
        std::vector<bool> seen(n, false);
        for (usize u : topoOrder_) seen[u] = true;
        for (usize i = 0; i < n; ++i)
            if (!seen[i]) topoOrder_.push_back(i);
    }
    topoDirty_ = false;
}

void Mixer::ComputeSoloAudibility() {
    // Fills the reused audible_ scratch. Its capacity only grows on the control thread (CreateBus),
    // so on the audio thread assign() never reallocates - no per-block heap allocation.
    std::vector<bool>& audible = audible_;
    const usize n = buses_.size();
    audible.assign(n, true);
    bool anySolo = false;
    for (const Bus& b : buses_)
        if (b.soloed) anySolo = true;
    if (!anySolo) return;

    audible.assign(n, false);
    auto idx = [this](BusId id) -> usize {
        auto it = index_.find(id);
        return it != index_.end() ? it->second : static_cast<usize>(-1);
    };
    // A bus is audible if it is a soloed bus, a DESCENDANT of one (walk up hits a solo), or an
    // ANCESTOR of one (so the soloed signal can route out to Master).
    for (usize i = 0; i < n; ++i) {
        usize cur = i;
        while (cur != static_cast<usize>(-1)) {
            if (buses_[cur].soloed) {
                audible[i] = true;
                break;
            }
            cur = (buses_[cur].id != master_) ? idx(buses_[cur].parent) : static_cast<usize>(-1);
        }
    }
    for (usize i = 0; i < n; ++i) {
        if (!buses_[i].soloed) continue;
        usize cur = i;
        while (cur != static_cast<usize>(-1)) {
            audible[cur] = true;
            cur = (buses_[cur].id != master_) ? idx(buses_[cur].parent) : static_cast<usize>(-1);
        }
    }
}

void Mixer::BeginBlock(u32 frameCount, u32 channels) {
    blockFrames_ = frameCount;
    blockChannels_ = channels;
    const usize n = static_cast<usize>(frameCount) * channels;
    for (Bus& b : buses_) b.buffer.assign(n, 0.0f);
}

f32* Mixer::BusBuffer(BusId bus) {
    Bus* b = Get(bus);
    if (b == nullptr) b = Get(master_);
    return b != nullptr && !b->buffer.empty() ? b->buffer.data() : nullptr;
}

void Mixer::EndBlock(f32* output, u32 frameCount, u32 channels, u32 sampleRate) {
    if (output == nullptr || frameCount == 0 || channels == 0) return;
    if (topoDirty_) RebuildTopology();

    ComputeSoloAudibility();
    const std::vector<bool>& audible = audible_;
    const bool anySolo = std::find(audible.begin(), audible.end(), false) != audible.end();
    const f32 blockDt = static_cast<f32>(frameCount) / static_cast<f32>(sampleRate);
    const usize count = static_cast<usize>(frameCount) * channels;

    std::memset(output, 0, count * sizeof(f32));

    for (usize idx : topoOrder_) {
        Bus& b = buses_[idx];

        f32 g = b.muted ? 0.0f : b.gain;
        if (anySolo && !audible[idx]) g = 0.0f;

        if (b.hasDuck) {
            const Bus* sc = Get(b.duckSidechain);
            const f32 scLevel = sc != nullptr ? sc->meterPeak : 0.0f;
            const f32 target = (scLevel > b.duckThreshold) ? (1.0f - b.duckAmount) : 1.0f;
            const f32 tauMs = (target < b.duckGain) ? b.duckAttackMs : b.duckReleaseMs;
            const f32 tau = tauMs / 1000.0f;
            const f32 coef = tau > 0.0f ? (1.0f - std::exp(-blockDt / tau)) : 1.0f;
            b.duckGain += (target - b.duckGain) * coef;
            g *= b.duckGain;
        }

        // Pre-fader insert effects: process the summed bus signal in chain order, in place.
        for (const std::unique_ptr<IAudioEffect>& fx : b.effects)
            if (fx) fx->Process(b.buffer.data(), frameCount, channels);

        f32 peak = 0.0f;
        const f32 g0 = b.smoothedGain;
        if (g0 == g) {
            // Steady gain: constant multiply (and skip entirely at unity).
            if (g != 1.0f) {
                for (usize i = 0; i < count; ++i) {
                    b.buffer[i] *= g;
                    const f32 a = std::fabs(b.buffer[i]);
                    if (a > peak) peak = a;
                }
            } else {
                for (usize i = 0; i < count; ++i) {
                    const f32 a = std::fabs(b.buffer[i]);
                    if (a > peak) peak = a;
                }
            }
        } else {
            // Ramp per FRAME from the previous gain to the target, eliminating zipper noise on
            // volume/mute/duck/solo changes (all folded into g).
            const f32 dg = (g - g0) / static_cast<f32>(frameCount);
            for (u32 f = 0; f < frameCount; ++f) {
                const f32 gc = g0 + dg * static_cast<f32>(f + 1);
                for (u32 c = 0; c < channels; ++c) {
                    const usize i = static_cast<usize>(f) * channels + c;
                    b.buffer[i] *= gc;
                    const f32 a = std::fabs(b.buffer[i]);
                    if (a > peak) peak = a;
                }
            }
        }
        b.smoothedGain = g;
        b.meterPeak = peak;

        if (b.id == master_) {
            std::memcpy(output, b.buffer.data(), count * sizeof(f32));
        } else {
            const BusId pid = (b.parent != kInvalidId) ? b.parent : master_;
            if (Bus* parent = Get(pid)) {
                for (usize i = 0; i < count; ++i) parent->buffer[i] += b.buffer[i];
            }
            for (const Send& s : b.sends) {
                Bus* dst = Get(s.dest);
                if (dst != nullptr && dst != &b) {
                    for (usize i = 0; i < count; ++i) dst->buffer[i] += b.buffer[i] * s.level;
                }
            }
        }
    }
}

} // namespace reverie
