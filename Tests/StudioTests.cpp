// Reverie/Tests/StudioTests.cpp - Reverie Studio authoring core (project + build + preview).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Verifies the project text round-trip (string + file), and that StudioApp builds a project into a
// live engine - resolving name/key references - so events/music/params preview correctly. Headless.
#include "StudioApp.h"
#include "StudioProject.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace reverie;
using namespace reverie::studio;

static int g_failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

static StudioProject MakeProject() {
    StudioProject p;
    p.assets.push_back({"Snd", "assets/snd.wav"});
    p.buses.push_back({"Combat", "Music", 0.7f, false, false});
    p.parameters.push_back({"Intensity", 0.0f, 0.0f, 1.0f, 0.0f});
    {
        StudioEvent e;
        e.name = "Fire";
        e.priority = 3;
        StudioEventLayer l;
        l.soundKey = "Snd";
        l.bus = "SFX";
        l.volume = 0.8f;
        e.layers.push_back(l);
        p.events.push_back(std::move(e));
    }
    {
        StudioMusicState m;
        m.name = "Battle";
        m.bpm = 130.0f;
        m.beatsPerBar = 4;
        m.layers.push_back({"Snd", 1.0f, "", 0.0f, 1.0f});                 // base
        m.layers.push_back({"Snd", 1.0f, "Intensity", 0.5f, 1.0f});        // param-gated perc
        p.music.push_back(std::move(m));
    }
    return p;
}

int main() {
    std::printf("reverie studio tests\n");
    const StudioProject p = MakeProject();

    // Text round-trip (string).
    StudioProject p2;
    Check(ReadProjectString(p2, WriteProjectString(p)), "project string round-trip parses");
    Check(p2.assets.size() == 1 && p2.buses.size() == 1 && p2.parameters.size() == 1 &&
              p2.events.size() == 1 && p2.music.size() == 1, "round-trip preserves counts");
    Check(p2.buses[0].name == "Combat" && p2.buses[0].parent == "Music" &&
              std::fabs(p2.buses[0].gain - 0.7f) < 1e-4f, "round-trip preserves bus fields");
    Check(p2.events[0].priority == 3 && p2.events[0].layers.size() == 1 &&
              p2.events[0].layers[0].soundKey == "Snd" && p2.events[0].layers[0].bus == "SFX",
          "round-trip preserves event + layer");
    Check(p2.music[0].layers.size() == 2 && p2.music[0].layers[1].gainParam == "Intensity" &&
              std::fabs(p2.music[0].layers[1].paramLo - 0.5f) < 1e-4f,
          "round-trip preserves music layer param binding");

    // File round-trip.
    Check(SaveProject(p, "studio_rt.rvproj"), "save project file");
    StudioProject p3;
    Check(LoadProject(p3, "studio_rt.rvproj") && p3.music.size() == 1 && p3.events.size() == 1,
          "load project file");

    // Build into a live engine + preview.
    StudioApp app;
    app.Project() = p2;
    Engine engine;
    Config cfg;
    cfg.backend = Backend::Null;
    cfg.sampleRate = 48000;
    cfg.channels = 2;
    Check(Succeeded(engine.Init(cfg)), "engine init");
    std::vector<f32> tone(4800);
    for (u32 i = 0; i < 4800; ++i)
        tone[i] = 0.4f * std::sin(2.0f * 3.14159265f * 330.0f * (f32)i / 48000.0f);
    const SoundId s = engine.LoadSoundPCM(tone.data(), 4800, 1, 48000);
    app.SetAssetSound("Snd", s);
    Check(app.Build(engine), "build project into engine");

    Check(app.Event("Fire") != kInvalidId, "event resolved");
    Check(app.Music("Battle") != kInvalidId, "music resolved");
    Check(app.Param("Intensity") != kInvalidId, "param resolved");
    Check(app.Bus("Combat") != kInvalidId, "custom bus resolved");

    // Preview the event.
    engine.PlayEvent(app.Event("Fire"), 1.0f);
    std::vector<f32> buf(512 * 2, 0.0f);
    double evEnergy = 0.0;
    for (int i = 0; i < 4; ++i) {
        engine.RenderOffline(buf.data(), 512);
        for (float x : buf) evEnergy += (double)x * x;
    }
    Check(evEnergy > 0.0, "event previews (audible)");
    engine.StopAll();

    // Preview music with the parameter driving the perc layer.
    engine.SetMusicState(app.Music("Battle"));
    auto musicEnergy = [&](f32 intensity) {
        engine.SetParameter(app.Param("Intensity"), intensity);
        for (int i = 0; i < 8; ++i) engine.RenderOffline(buf.data(), 512);
        std::fill(buf.begin(), buf.end(), 0.0f);
        engine.RenderOffline(buf.data(), 512);
        double e = 0.0;
        for (float x : buf) e += (double)x * x;
        return e;
    };
    const double lo = musicEnergy(0.0f);
    const double hi = musicEnergy(1.0f);
    Check(lo > 0.0 && hi > lo * 1.2, "music previews + Intensity brings the perc layer in");
    engine.Shutdown();

    if (g_failures == 0) {
        std::printf("reverie studio tests: PASS\n");
        return 0;
    }
    std::printf("reverie studio tests: FAIL (%d)\n", g_failures);
    return 1;
}
