// Reverie/Tests/MusicTests.cpp - adaptive music: clock, states, parameter-driven layer gains.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Verifies the musical clock advances at the state's tempo, that a music state plays its layers,
// that a parameter drives a layer's gain in/out (the RTPC consumer, end to end), and that
// StopMusic tears the layers down. Then a C-ABI smoke test. Headless (Null backend), deterministic.
#include "Reverie/Reverie.h"
#include "reverie.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace reverie;

static int g_failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

static constexpr u32 kSr = 48000;
static constexpr u32 kBlock = 512;

static std::vector<f32> MakeSine(f32 hz, u32 frames) {
    std::vector<f32> s(frames, 0.0f);
    for (u32 i = 0; i < frames; ++i)
        s[i] = 0.4f * std::sin(2.0f * 3.14159265f * hz * (f32)i / (f32)kSr);
    return s;
}

static double RenderEnergy(Engine& e, int blocks) {
    std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);
    double energy = 0.0;
    for (int i = 0; i < blocks; ++i) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        e.RenderOffline(buf.data(), kBlock);
        if (i == blocks - 1)
            for (float x : buf) energy += (double)x * x;
    }
    return energy;
}

int main() {
    std::printf("reverie music tests\n");

    Engine engine;
    Config cfg;
    cfg.backend = Backend::Null;
    cfg.sampleRate = kSr;
    cfg.channels = 2;
    Check(Succeeded(engine.Init(cfg)), "init(Null)");

    const std::vector<f32> baseTone = MakeSine(200.0f, 4800);
    const std::vector<f32> percTone = MakeSine(400.0f, 4800);
    const SoundId baseSnd = engine.LoadSoundPCM(baseTone.data(), (u32)baseTone.size(), 1, kSr);
    const SoundId percSnd = engine.LoadSoundPCM(percTone.data(), (u32)percTone.size(), 1, kSr);

    const ParameterId intensity = engine.RegisterParameter("Intensity", 0.0f, 0.0f, 1.0f, 0.0f);

    MusicStateDesc combat;
    combat.name = "Combat";
    combat.bpm = 120.0f;
    combat.beatsPerBar = 4;
    combat.layers.push_back(MusicLayerDesc{baseSnd, 1.0f, kInvalidId, 0.0f, 1.0f});          // always on
    combat.layers.push_back(MusicLayerDesc{percSnd, 1.0f, intensity, 0.5f, 1.0f});           // param-gated
    const MusicStateId cid = engine.RegisterMusicState(combat);
    Check(cid != kInvalidId, "register music state");
    Check(engine.FindMusicState("Combat") == cid, "find music state by name");

    // Clock does not advance until a state is playing.
    RenderEnergy(engine, 4);
    Check(engine.MusicBeat() == 0.0, "clock idle before SetMusicState");

    engine.SetMusicState(cid);
    Check(engine.CurrentMusicState() == cid, "current state set");
    Check(engine.MusicBpm() == 120.0f, "tempo adopted from state");

    // Intensity 0 (< lo 0.5): percussion layer muted -> only the base tone.
    engine.SetParameter(intensity, 0.0f);
    const double eLow = RenderEnergy(engine, 20);
    Check(eLow > 0.0, "base layer plays");

    // Intensity 1: percussion layer full -> more energy.
    engine.SetParameter(intensity, 1.0f);
    const double eHigh = RenderEnergy(engine, 20);
    Check(eHigh > 1.3 * eLow, "raising Intensity brings the percussion layer in (param-driven gain)");

    // Clock advanced at 120 BPM = 2 beats/sec. After ~2.13s we should be in bar 1.
    // (RenderEnergy above rendered 4+20+20 = 44 blocks while playing... plus the first idle 4 did
    //  not count.) Render more to a known point and check the tempo math.
    engine.SetMusicState(cid); // restart clock state is continuous; just verify it keeps advancing
    const double beatBefore = engine.MusicBeat();
    RenderEnergy(engine, 200); // 200 * 512 / 48000 = 2.1333 s -> +4.2667 beats
    const double beatAfter = engine.MusicBeat();
    const double delta = beatAfter - beatBefore;
    Check(std::fabs(delta - 4.2667) < 0.02, "clock advances at the state tempo (120 BPM)");
    Check(engine.MusicBar() >= 1, "bar counter advances");

    // Stop tears the layers down.
    engine.StopMusic();
    Check(engine.CurrentMusicState() == kInvalidId, "current state cleared on stop");
    RenderEnergy(engine, 8); // let the audio thread reap the stopped voices
    Check(engine.ActiveVoiceCount() == 0, "music voices drained after StopMusic");

    engine.Shutdown();

    // --- bar-quantized transition (applied by Update at the boundary) ---
    {
        Engine e3;
        Config c3;
        c3.backend = Backend::Null;
        c3.sampleRate = kSr;
        c3.channels = 2;
        e3.Init(c3);
        const std::vector<f32> tone = MakeSine(220.0f, 4800);
        const SoundId s = e3.LoadSoundPCM(tone.data(), (u32)tone.size(), 1, kSr);
        MusicStateDesc A;
        A.name = "A"; A.bpm = 120.0f; A.beatsPerBar = 4;
        A.layers.push_back(MusicLayerDesc{s, 1.0f, kInvalidId, 0.0f, 1.0f});
        MusicStateDesc B = A;
        B.name = "B";
        const MusicStateId a = e3.RegisterMusicState(A);
        const MusicStateId b = e3.RegisterMusicState(B);

        e3.SetMusicState(a); // immediate
        std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);
        for (int i = 0; i < 20; ++i) { e3.RenderOffline(buf.data(), kBlock); e3.Update(); }
        Check(e3.CurrentMusicState() == a, "state A active after immediate set");

        e3.SetMusicState(b, MusicTransition::NextBar); // defer to bar boundary (beat 4)
        Check(e3.CurrentMusicState() == a, "quantized transition not applied immediately");

        bool switched = false;
        double switchBeat = 0.0;
        for (int i = 0; i < 4000 && !switched; ++i) {
            e3.RenderOffline(buf.data(), kBlock);
            e3.Update();
            if (e3.CurrentMusicState() == b) { switched = true; switchBeat = e3.MusicBeat(); }
        }
        Check(switched, "quantized transition eventually applied by Update");
        Check(switchBeat >= 3.95 && switchBeat < 4.2,
              "switched right at the next bar boundary (beat 4), not before or long after");
        e3.Shutdown();
    }

    // --- C ABI ---
    {
        reverie_engine* e = reverie_create();
        reverie_config c;
        reverie_default_config(&c);
        c.backend = REVERIE_BACKEND_NULL;
        reverie_init(e, &c);
        std::vector<f32> t = MakeSine(300.0f, 2400);
        const reverie_sound s = reverie_load_sound_pcm(e, t.data(), (u32)t.size(), 1, kSr);
        reverie_music_builder* b = reverie_music_builder_create("Explore", 100.0f, 4);
        reverie_music_builder_add_layer(b, s, 1.0f, 0u, 0.0f, 1.0f);
        const reverie_music_state ms = reverie_music_builder_register(e, b);
        Check(ms != 0u, "C ABI: register music state");
        reverie_set_music_state(e, ms);
        Check(reverie_music_bpm(e) == 100.0f, "C ABI: tempo");
        std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);
        for (int i = 0; i < 10; ++i) reverie_render_offline(e, buf.data(), kBlock);
        Check(reverie_music_beat(e) > 0.0, "C ABI: clock advanced");
        reverie_shutdown(e);
        reverie_destroy(e);
    }

    if (g_failures == 0) {
        std::printf("reverie music tests: PASS\n");
        return 0;
    }
    std::printf("reverie music tests: FAIL (%d)\n", g_failures);
    return 1;
}
