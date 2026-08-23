// Reverie/Studio/main.cpp - reverie-studio: the authoring app entry point.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Headless commands for the Studio authoring core (a Dear ImGui window is a thin view over the same
// StudioApp/StudioProject):
//   reverie-studio new <project.rvproj>     write a starter project
//   reverie-studio info <project.rvproj>     load + summarize a project
//   reverie-studio --selftest                build a project + preview it in-memory (CI)
#include "StudioApp.h"
#include "StudioProject.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace reverie;
using namespace reverie::studio;

static StudioProject SampleProject() {
    StudioProject p;
    p.assets.push_back({"Music.Battle.Base", "music/battle_base.wav"});
    p.assets.push_back({"Music.Battle.Perc", "music/battle_perc.wav"});
    p.assets.push_back({"Weapons.Rifle.Fire", "sfx/rifle_fire.wav"});
    p.buses.push_back({"Combat", "Music", 0.9f, false, false});
    p.parameters.push_back({"CombatIntensity", 0.0f, 0.0f, 1.0f, 250.0f});
    {
        StudioEvent e;
        e.name = "Weapons.Rifle.Fire";
        StudioEventLayer l;
        l.soundKey = "Weapons.Rifle.Fire";
        l.bus = "SFX";
        e.layers.push_back(l);
        p.events.push_back(std::move(e));
    }
    {
        StudioMusicState m;
        m.name = "Battle";
        m.bpm = 120.0f;
        m.beatsPerBar = 4;
        m.layers.push_back({"Music.Battle.Base", 1.0f, "", 0.0f, 1.0f});
        m.layers.push_back({"Music.Battle.Perc", 1.0f, "CombatIntensity", 0.5f, 1.0f});
        p.music.push_back(std::move(m));
    }
    return p;
}

static int SelfTest() {
    // Round-trip a sample project through text.
    const StudioProject p = SampleProject();
    StudioProject p2;
    if (!ReadProjectString(p2, WriteProjectString(p))) {
        std::printf("reverie-studio selftest: FAIL (project round-trip)\n");
        return 1;
    }
    // Build into a headless engine, injecting a tone for every asset, then preview.
    StudioApp app;
    app.Project() = p2;
    Engine engine;
    Config cfg;
    cfg.backend = Backend::Null;
    if (Failed(engine.Init(cfg))) {
        std::printf("reverie-studio selftest: FAIL (engine init)\n");
        return 1;
    }
    std::vector<f32> tone(4800);
    for (u32 i = 0; i < 4800; ++i) tone[i] = 0.4f * std::sin(2.0f * 3.14159265f * 330.0f * (f32)i / 48000.0f);
    const SoundId s = engine.LoadSoundPCM(tone.data(), 4800, 1, 48000);
    for (const StudioAsset& a : p2.assets) app.SetAssetSound(a.key, s);
    app.Build(engine);

    const EventId ev = app.Event("Weapons.Rifle.Fire");
    const MusicStateId ms = app.Music("Battle");
    if (ev == kInvalidId || ms == kInvalidId) {
        std::printf("reverie-studio selftest: FAIL (build did not resolve event/music)\n");
        return 1;
    }
    engine.SetMusicState(ms);
    engine.PlayEvent(ev, 1.0f);
    std::vector<f32> buf(512 * 2, 0.0f);
    double energy = 0.0;
    for (int i = 0; i < 8; ++i) {
        engine.RenderOffline(buf.data(), 512);
        for (float x : buf) energy += (double)x * x;
    }
    engine.Shutdown();
    if (energy <= 0.0) {
        std::printf("reverie-studio selftest: FAIL (no preview audio)\n");
        return 1;
    }
    std::printf("reverie-studio selftest: PASS (built %zu assets / %zu events / %zu music states)\n",
                p2.assets.size(), p2.events.size(), p2.music.size());
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--selftest") return SelfTest();
    if (argc == 3 && std::string(argv[1]) == "new") {
        if (!SaveProject(SampleProject(), argv[2])) {
            std::printf("reverie-studio: cannot write '%s'\n", argv[2]);
            return 1;
        }
        std::printf("reverie-studio: wrote starter project '%s'\n", argv[2]);
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "info") {
        StudioProject p;
        if (!LoadProject(p, argv[2])) {
            std::printf("reverie-studio: cannot load '%s'\n", argv[2]);
            return 1;
        }
        std::printf("project '%s': %zu assets, %zu buses, %zu params, %zu events, %zu music states\n",
                    argv[2], p.assets.size(), p.buses.size(), p.parameters.size(), p.events.size(),
                    p.music.size());
        return 0;
    }
    std::printf("usage:\n  reverie-studio new <project.rvproj>\n  reverie-studio info <project.rvproj>\n"
                "  reverie-studio --selftest\n");
    return 2;
}
