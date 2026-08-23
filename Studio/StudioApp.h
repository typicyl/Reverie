// Reverie/Studio/StudioApp.h - Reverie Studio application core (authoring <-> runtime bridge).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Holds a StudioProject and BUILDS it into a live reverie::Engine for preview: it loads assets,
// creates the bus tree, registers parameters, events, and music, and resolves the project's
// name/key references into runtime ids. This is the substance of the Studio app (the authoring
// workflow + the project->runtime pipeline); a Dear ImGui window is a thin view over exactly this
// model and these operations. Uses only the public SDK.
#pragma once

#include "Reverie/Reverie.h"
#include "StudioProject.h"

#include <string>
#include <unordered_map>

namespace reverie::studio {

class StudioApp {
public:
    bool Load(const std::string& path) { return LoadProject(project_, path); }
    bool LoadFromString(const std::string& text) { return ReadProjectString(project_, text); }
    bool Save(const std::string& path) const { return SaveProject(project_, path); }

    const StudioProject& Project() const { return project_; }
    StudioProject& Project() { return project_; }

    // Inject a pre-loaded sound for an asset key (used by hosts/tests that load their own audio, or
    // to preview without touching the filesystem). Call before Build.
    void SetAssetSound(const std::string& key, SoundId sound) { sounds_[key] = sound; }

    // Build the project into `engine`: assets (injected or loaded from their path), buses,
    // parameters, events, music - resolving all name/key references. Returns false only on a hard
    // failure; unresolved references degrade gracefully (missing sound -> silent layer, etc.).
    bool Build(Engine& engine);

    // Resolved ids (valid after Build).
    SoundId Sound(const std::string& key) const { return Lookup(sounds_, key); }
    BusId Bus(const std::string& name) const { return Lookup(buses_, name); }
    ParameterId Param(const std::string& name) const { return Lookup(params_, name); }
    EventId Event(const std::string& name) const { return Lookup(events_, name); }
    MusicStateId Music(const std::string& name) const { return Lookup(music_, name); }

private:
    static u32 Lookup(const std::unordered_map<std::string, u32>& m, const std::string& k) {
        auto it = m.find(k);
        return it != m.end() ? it->second : kInvalidId;
    }
    BusId ResolveBus(Engine& engine, const std::string& name) const;
    ParameterId ResolveParam(Engine& engine, const std::string& name) const;

    StudioProject project_;
    std::unordered_map<std::string, SoundId> sounds_;
    std::unordered_map<std::string, BusId> buses_;
    std::unordered_map<std::string, ParameterId> params_;
    std::unordered_map<std::string, EventId> events_;
    std::unordered_map<std::string, MusicStateId> music_;
};

} // namespace reverie::studio
