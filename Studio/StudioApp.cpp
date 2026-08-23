// Reverie/Studio/StudioApp.cpp - see StudioApp.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "StudioApp.h"

namespace reverie::studio {

BusId StudioApp::ResolveBus(Engine& engine, const std::string& name) const {
    if (name.empty()) return kInvalidId; // Master
    auto it = buses_.find(name);
    if (it != buses_.end()) return it->second;
    return engine.FindBus(name.c_str()); // e.g. a default bus (SFX/Music/...); kInvalidId -> Master
}

ParameterId StudioApp::ResolveParam(Engine& engine, const std::string& name) const {
    if (name.empty()) return kInvalidId;
    auto it = params_.find(name);
    if (it != params_.end()) return it->second;
    return engine.FindParameter(name.c_str());
}

bool StudioApp::Build(Engine& engine) {
    buses_.clear();
    params_.clear();
    events_.clear();
    music_.clear();
    // (sounds_ keeps any injected entries.)

    // Buses (project order; parents should precede children - CreateBus resolves parent by name).
    for (const StudioBus& b : project_.buses) {
        const BusId parent = b.parent.empty() ? engine.MasterBus() : ResolveBus(engine, b.parent);
        const BusId id = engine.CreateBus(b.name.c_str(), parent);
        if (id != kInvalidId) {
            engine.SetBusVolume(id, b.gain);
            engine.SetBusMuted(id, b.muted);
            engine.SetBusSoloed(id, b.soloed);
            buses_[b.name] = id;
        }
    }

    // Parameters.
    for (const StudioParam& p : project_.parameters) {
        const ParameterId id =
            engine.RegisterParameter(p.name.c_str(), p.defaultValue, p.minValue, p.maxValue, p.smoothMs);
        if (id != kInvalidId) params_[p.name] = id;
    }

    // Assets: load any not already injected.
    for (const StudioAsset& a : project_.assets) {
        if (sounds_.find(a.key) != sounds_.end()) continue; // injected
        SoundId s = kInvalidId;
        if (!a.path.empty()) s = engine.LoadSoundFile(a.path.c_str());
        sounds_[a.key] = s; // may be kInvalidId (missing/failed) -> layers referencing it stay silent
    }

    // Events.
    for (const StudioEvent& e : project_.events) {
        EventDesc desc;
        desc.priority = e.priority;
        desc.maxInstances = e.maxInstances;
        desc.concurrencyGroup = e.concurrencyGroup;
        desc.layers.reserve(e.layers.size());
        for (const StudioEventLayer& l : e.layers) {
            EventLayerDesc ld;
            ld.pool.push_back({Sound(l.soundKey), 1.0f});
            ld.volume = l.volume;
            ld.volumeVariance = l.volumeVariance;
            ld.pitch = l.pitch;
            ld.pitchVariance = l.pitchVariance;
            ld.loop = l.loop;
            ld.probability = l.probability;
            ld.bus = ResolveBus(engine, l.bus);
            ld.gainParam = ResolveParam(engine, l.gainParam);
            ld.paramLo = l.paramLo;
            ld.paramHi = l.paramHi;
            desc.layers.push_back(std::move(ld));
        }
        const EventId id = engine.RegisterEvent(desc);
        if (id != kInvalidId) events_[e.name] = id;
    }

    // Music states.
    for (const StudioMusicState& m : project_.music) {
        MusicStateDesc desc;
        desc.name = m.name;
        desc.bpm = m.bpm;
        desc.beatsPerBar = m.beatsPerBar;
        desc.layers.reserve(m.layers.size());
        for (const StudioMusicLayer& k : m.layers) {
            MusicLayerDesc kd;
            kd.sound = Sound(k.soundKey);
            kd.gain = k.gain;
            kd.gainParam = ResolveParam(engine, k.gainParam);
            kd.paramLo = k.paramLo;
            kd.paramHi = k.paramHi;
            desc.layers.push_back(kd);
        }
        const MusicStateId id = engine.RegisterMusicState(desc);
        if (id != kInvalidId) music_[m.name] = id;
    }

    return true;
}

} // namespace reverie::studio
