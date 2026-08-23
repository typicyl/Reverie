// Reverie/Studio/StudioProject.h - the Reverie Studio authoring document.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A StudioProject is the editable, source-control-friendly authoring model: named assets, the mixer
// bus tree, game parameters, events, and adaptive-music states - all keyed by NAME (not runtime
// ids), so it round-trips through a human-readable text file. Studio edits this; StudioApp builds it
// into a live reverie::Engine (preview) and, later, cooks it into runtime banks. This layer speaks
// ONLY the public SDK vocabulary (no runtime internals).
#pragma once

#include "Reverie/Reverie.h"

#include <string>
#include <vector>

namespace reverie::studio {

struct StudioAsset {
    std::string key;  // stable name events/music reference (e.g. "Weapons.Rifle.Fire")
    std::string path; // source audio file (wav/flac/mp3/ogg) - may contain spaces
};

struct StudioBus {
    std::string name;
    std::string parent; // empty = Master
    f32 gain = 1.0f;
    bool muted = false;
    bool soloed = false;
};

struct StudioParam {
    std::string name;
    f32 defaultValue = 0.0f, minValue = 0.0f, maxValue = 1.0f, smoothMs = 0.0f;
};

struct StudioEventLayer {
    std::string soundKey;
    f32 volume = 1.0f, volumeVariance = 0.0f, pitch = 1.0f, pitchVariance = 0.0f;
    bool loop = false;
    f32 probability = 1.0f;
    std::string bus;       // empty = Master
    std::string gainParam; // empty = none
    f32 paramLo = 0.0f, paramHi = 1.0f;
};

struct StudioEvent {
    std::string name;
    i32 priority = 0;
    u32 maxInstances = 0;
    u32 concurrencyGroup = 0;
    std::vector<StudioEventLayer> layers;
};

struct StudioMusicLayer {
    std::string soundKey;
    f32 gain = 1.0f;
    std::string gainParam; // empty = none
    f32 paramLo = 0.0f, paramHi = 1.0f;
};

struct StudioMusicState {
    std::string name;
    f32 bpm = 120.0f;
    u32 beatsPerBar = 4;
    std::vector<StudioMusicLayer> layers;
};

struct StudioProject {
    std::vector<StudioAsset> assets;
    std::vector<StudioBus> buses;
    std::vector<StudioParam> parameters;
    std::vector<StudioEvent> events;
    std::vector<StudioMusicState> music;
};

// Text serialization (versioned, line-based; identifiers are space-free, asset paths are last on
// their line so they may contain spaces).
std::string WriteProjectString(const StudioProject& project);
bool ReadProjectString(StudioProject& out, const std::string& text);
bool SaveProject(const StudioProject& project, const std::string& path);
bool LoadProject(StudioProject& out, const std::string& path);

} // namespace reverie::studio
