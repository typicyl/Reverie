// Reverie/Tests/ParamTests.cpp - the game-parameter (RTPC) store.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Verifies registration/lookup, range clamping, instant vs smoothed easing (advanced by rendering
// blocks through the Null backend), and the flat C ABI. Deterministic and headless.
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
static constexpr u32 kBlock = 256;

int main() {
    std::printf("reverie parameter tests\n");

    Engine engine;
    Config cfg;
    cfg.backend = Backend::Null;
    cfg.sampleRate = kSr;
    cfg.channels = 2;
    Check(Succeeded(engine.Init(cfg)), "init(Null)");
    std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);

    // --- registration / lookup / idempotence ---
    const ParameterId combat =
        engine.RegisterParameter("CombatIntensity", /*def*/ 0.0f, /*min*/ 0.0f, /*max*/ 1.0f,
                                 /*smoothMs*/ 100.0f);
    Check(combat != kInvalidId, "register returns a valid id");
    Check(engine.FindParameter("CombatIntensity") == combat, "find by name returns the same id");
    Check(engine.RegisterParameter("CombatIntensity", 0.5f, 0.0f, 2.0f, 0.0f) == combat,
          "re-register same name returns the same id (idempotent)");
    Check(engine.FindParameter("Nope") == kInvalidId, "find unknown name -> invalid");
    Check(engine.ParameterValue(combat) == 0.0f, "initial value is the default");

    // --- clamping ---
    engine.SetParameter(combat, 5.0f);
    Check(engine.ParameterTarget(combat) == 1.0f, "target clamps to max");
    engine.SetParameter(combat, -5.0f);
    Check(engine.ParameterTarget(combat) == 0.0f, "target clamps to min");

    // --- smoothed easing toward a target ---
    engine.SetParameter(combat, 1.0f);
    Check(engine.ParameterValue(combat) == 0.0f, "value not yet moved before any render");
    engine.RenderOffline(buf.data(), kBlock); // one block advances smoothing a little
    const f32 afterOne = engine.ParameterValue(combat);
    Check(afterOne > 0.0f && afterOne < 1.0f, "one block -> partway toward target (monotonic ease)");
    for (int i = 0; i < 400; ++i) engine.RenderOffline(buf.data(), kBlock); // ~2.1s at 100ms tau
    Check(engine.ParameterValue(combat) > 0.999f, "converges to target after many blocks");

    // --- instant (smoothMs == 0) parameter snaps in one block ---
    const ParameterId health = engine.RegisterParameter("Health", 100.0f, 0.0f, 100.0f, 0.0f);
    Check(health != kInvalidId && health != combat, "second parameter gets a distinct id");
    engine.SetParameter(health, 42.0f);
    engine.RenderOffline(buf.data(), kBlock);
    Check(engine.ParameterValue(health) == 42.0f, "smoothMs==0 snaps to target in one block");

    // --- invalid id is a safe no-op / zero ---
    Check(engine.ParameterValue(9999u) == 0.0f, "invalid id value -> 0");
    engine.SetParameter(9999u, 1.0f); // must not crash
    Check(true, "set on invalid id is a safe no-op");

    engine.Shutdown();

    // --- parameter -> bus-gain automation (mixer RTPC consumer) ---
    {
        Engine e2;
        Config c2;
        c2.backend = Backend::Null;
        c2.sampleRate = kSr;
        c2.channels = 2;
        e2.Init(c2);
        std::vector<f32> tone(4800, 0.0f);
        for (u32 i = 0; i < 4800; ++i)
            tone[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * (f32)i / (f32)kSr);
        const SoundId s = e2.LoadSoundPCM(tone.data(), 4800, 1, kSr);
        const ParameterId mv = e2.RegisterParameter("MasterVol", 1.0f, 0.0f, 1.0f, 0.0f);
        Check(e2.BindParameterToBusGain(mv, e2.MasterBus()), "bind parameter to master gain");
        e2.Play(s, 1.0f, true);
        std::vector<f32> b(static_cast<usize>(kBlock) * 2, 0.0f);
        auto energyAfter = [&](int warm) {
            for (int i = 0; i < warm; ++i) e2.RenderOffline(b.data(), kBlock);
            std::fill(b.begin(), b.end(), 0.0f);
            e2.RenderOffline(b.data(), kBlock);
            double en = 0.0;
            for (float x : b) en += (double)x * x;
            return en;
        };
        e2.SetParameter(mv, 1.0f);
        const double eFull = energyAfter(4);
        e2.SetParameter(mv, 0.25f);
        const double eQuarter = energyAfter(4);
        Check(eFull > 0.0 && eQuarter < 0.2 * eFull, "parameter drives the master bus gain");
        e2.Shutdown();
    }

    // --- C ABI ---
    {
        reverie_engine* e = reverie_create();
        reverie_config c;
        reverie_default_config(&c);
        c.backend = REVERIE_BACKEND_NULL;
        Check(reverie_init(e, &c) == REVERIE_OK, "C ABI: init");
        const reverie_parameter p =
            reverie_register_parameter(e, "Danger", 0.0f, 0.0f, 1.0f, 0.0f);
        Check(p != 0u, "C ABI: register parameter");
        Check(reverie_find_parameter(e, "Danger") == p, "C ABI: find parameter");
        reverie_set_parameter(e, p, 0.75f);
        Check(reverie_get_parameter_target(e, p) == 0.75f, "C ABI: target set");
        std::vector<f32> cbuf(static_cast<usize>(kBlock) * 2, 0.0f);
        reverie_render_offline(e, cbuf.data(), kBlock);
        Check(reverie_get_parameter(e, p) == 0.75f, "C ABI: instant param applied after a block");
        reverie_shutdown(e);
        reverie_destroy(e);
    }

    if (g_failures == 0) {
        std::printf("reverie parameter tests: PASS\n");
        return 0;
    }
    std::printf("reverie parameter tests: FAIL (%d)\n", g_failures);
    return 1;
}
