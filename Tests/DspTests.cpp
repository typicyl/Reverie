// Reverie/Tests/DspTests.cpp - per-bus DSP insert chain + biquad filter.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Plays a high-frequency tone through the Master bus and checks that a lowpass insert strongly
// attenuates it, a highpass lets it through, and the parameter getters reflect what was set. Then
// smoke-tests the flat C ABI. Headless (Null backend), deterministic (energy comparisons).
#include "Reverie/Reverie.h"
#include "reverie.h"

#include <algorithm>
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
        s[i] = 0.5f * std::sin(2.0f * 3.14159265f * hz * (f32)i / (f32)kSr);
    return s;
}

// Energy of one freshly rendered block after `warmup` blocks (lets the voice + filter settle).
static double BlockEnergy(Engine& e, int warmup) {
    std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);
    for (int i = 0; i < warmup; ++i) e.RenderOffline(buf.data(), kBlock);
    std::fill(buf.begin(), buf.end(), 0.0f);
    e.RenderOffline(buf.data(), kBlock);
    double energy = 0.0;
    for (float x : buf) energy += (double)x * x;
    return energy;
}

static f32 BlockPeak(Engine& e, int warmup) {
    std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);
    for (int i = 0; i < warmup; ++i) e.RenderOffline(buf.data(), kBlock);
    std::fill(buf.begin(), buf.end(), 0.0f);
    e.RenderOffline(buf.data(), kBlock);
    f32 peak = 0.0f;
    for (float x : buf) peak = std::max(peak, std::fabs(x));
    return peak;
}

int main() {
    std::printf("reverie dsp tests\n");

    Engine engine;
    Config cfg;
    cfg.backend = Backend::Null;
    cfg.sampleRate = kSr;
    cfg.channels = 2;
    Check(Succeeded(engine.Init(cfg)), "init(Null)");

    // A continuous 8 kHz tone on Master.
    const std::vector<f32> tone = MakeSine(8000.0f, 4800);
    const SoundId snd = engine.LoadSoundPCM(tone.data(), (u32)tone.size(), 1, kSr);
    Check(snd != kInvalidId, "load 8kHz tone");
    engine.Play(snd, 1.0f, /*loop*/ true);

    const double eUnfiltered = BlockEnergy(engine, 8);
    Check(eUnfiltered > 1.0, "unfiltered tone has energy");

    // Insert a lowpass at 300 Hz on Master: an 8 kHz tone is ~4.7 octaves up -> heavy attenuation.
    const EffectId fx = engine.AddBusEffect(engine.MasterBus(), EffectType::Filter);
    Check(fx != kInvalidId, "add filter effect");
    engine.SetEffectParam(fx, 0, (f32)FilterType::Lowpass);
    engine.SetEffectParam(fx, 1, 300.0f); // cutoff
    engine.SetEffectParam(fx, 2, 0.707f); // Q
    Check(engine.EffectParam(fx, 1) == 300.0f, "effect cutoff getter reflects set");
    Check((u32)engine.EffectParam(fx, 0) == (u32)FilterType::Lowpass, "effect type getter reflects set");

    const double eLowpassed = BlockEnergy(engine, 8);
    Check(eLowpassed < 0.05 * eUnfiltered, "lowpass at 300Hz strongly attenuates the 8kHz tone");

    // Switch the same effect to a highpass at 300 Hz: the 8 kHz tone now passes.
    engine.SetEffectParam(fx, 0, (f32)FilterType::Highpass);
    const double eHighpassed = BlockEnergy(engine, 8);
    Check(eHighpassed > 0.5 * eUnfiltered, "highpass at 300Hz passes the 8kHz tone");

    engine.Shutdown();

    // --- Compressor as a limiter: reduces the peak of an over-threshold tone ---
    {
        Engine c;
        Config cfg2;
        cfg2.backend = Backend::Null;
        cfg2.sampleRate = kSr;
        cfg2.channels = 2;
        c.Init(cfg2);
        const std::vector<f32> tone = MakeSine(1000.0f, 4800); // 0.5 amplitude
        const SoundId t = c.LoadSoundPCM(tone.data(), (u32)tone.size(), 1, kSr);
        c.Play(t, 1.0f, true);
        const f32 peakDry = BlockPeak(c, 8);
        Check(peakDry > 0.3f, "dry tone peak present");
        const EffectId comp = c.AddBusEffect(c.MasterBus(), EffectType::Compressor);
        c.SetEffectParam(comp, 0, -20.0f); // threshold dB (~0.1 linear)
        c.SetEffectParam(comp, 1, 20.0f);  // ratio -> limiter
        c.SetEffectParam(comp, 2, 1.0f);   // fast attack
        c.SetEffectParam(comp, 3, 50.0f);  // release
        const f32 peakComp = BlockPeak(c, 16);
        Check(peakComp < peakDry * 0.6f, "compressor/limiter reduces the peak");
        c.Shutdown();
    }

    // --- Delay: a feedback echo persists after the source ends ---
    {
        Engine d;
        Config cfg3;
        cfg3.backend = Backend::Null;
        cfg3.sampleRate = kSr;
        cfg3.channels = 2;
        d.Init(cfg3);
        const std::vector<f32> burst = MakeSine(500.0f, 2000); // ~42ms, non-looping
        const SoundId b = d.LoadSoundPCM(burst.data(), (u32)burst.size(), 1, kSr);
        const EffectId dly = d.AddBusEffect(d.MasterBus(), EffectType::Delay);
        d.SetEffectParam(dly, 0, 50.0f); // 50 ms
        d.SetEffectParam(dly, 1, 0.6f);  // feedback
        d.SetEffectParam(dly, 2, 1.0f);  // full wet
        Check(d.EffectParam(dly, 1) == 0.6f, "delay feedback param roundtrip");
        d.Play(b, 1.0f, false);
        std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);
        // Render well past the 42ms source so only delayed echoes remain, then measure energy.
        for (int i = 0; i < 20; ++i) { std::fill(buf.begin(), buf.end(), 0.0f); d.RenderOffline(buf.data(), kBlock); }
        double tail = 0.0;
        for (float x : buf) tail += (double)x * x;
        Check(tail > 0.0, "delay produces an echo tail after the source ends");
        d.Shutdown();
    }

    // --- C ABI ---
    {
        reverie_engine* e = reverie_create();
        reverie_config c;
        reverie_default_config(&c);
        c.backend = REVERIE_BACKEND_NULL;
        Check(reverie_init(e, &c) == REVERIE_OK, "C ABI: init");
        const reverie_effect cfx =
            reverie_add_bus_effect(e, reverie_master_bus(e), REVERIE_EFFECT_FILTER);
        Check(cfx != 0u, "C ABI: add effect");
        reverie_set_effect_param(e, cfx, 0, (float)REVERIE_FILTER_HIGHSHELF);
        reverie_set_effect_param(e, cfx, 1, 2000.0f);
        Check(reverie_get_effect_param(e, cfx, 1) == 2000.0f, "C ABI: effect param roundtrip");
        reverie_shutdown(e);
        reverie_destroy(e);
    }

    if (g_failures == 0) {
        std::printf("reverie dsp tests: PASS\n");
        return 0;
    }
    std::printf("reverie dsp tests: FAIL (%d)\n", g_failures);
    return 1;
}
