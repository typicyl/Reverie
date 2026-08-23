// Reverie/Tests/MixerTests.cpp - Phase 3 deterministic headless test.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Proves the bus tree: default buses, voice->bus routing + per-bus volume/mute, solo, per-bus
// metering, sidechain ducking (dialogue ducks music), snapshots, and sends. Headless / Null
// backend, deterministic. Also smoke-tests the C ABI bus surface.
#include "Reverie/Reverie.h"
#include "reverie.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
int g_failures = 0;

void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

std::vector<float> MakeSine(unsigned frames, unsigned sampleRate, float freq, float amp) {
    std::vector<float> s(frames);
    for (unsigned i = 0; i < frames; ++i)
        s[i] = amp * static_cast<float>(std::sin(2.0 * kPi * freq * i / sampleRate));
    return s;
}

double Energy(const std::vector<float>& buf) {
    double e = 0.0;
    for (float x : buf) e += double(x) * x;
    return e;
}

// A looping 1-layer event routed to `bus` at `vol`.
reverie::EventId BusEvent(reverie::Engine& e, reverie::SoundId snd, reverie::BusId bus, float vol) {
    reverie::EventLayerDesc l;
    l.pool.push_back({snd, 1.0f});
    l.volume = vol;
    l.loop = true;
    l.bus = bus;
    reverie::EventDesc d;
    d.layers.push_back(l);
    return e.RegisterEvent(d);
}

} // namespace

int main() {
    using namespace reverie;

    const unsigned kSr = 48000, kCh = 2, kBlock = 512;
    const std::vector<float> sine = MakeSine(1024, kSr, 440.0f, 0.5f);

    Engine engine;
    Check(engine.Init({Backend::Null, kSr, kCh, 0, 64}) == Result::Ok, "Init");
    const SoundId snd = engine.LoadSoundPCM(sine.data(), (unsigned)sine.size(), 1, kSr);

    // 1) Default tree exists.
    const BusId master = engine.MasterBus();
    const BusId music = engine.FindBus("Music");
    const BusId sfx = engine.FindBus("SFX");
    const BusId dialogue = engine.FindBus("Dialogue");
    Check(master != kInvalidId, "Master bus exists");
    Check(music != kInvalidId && sfx != kInvalidId && dialogue != kInvalidId,
          "default sub-buses exist");

    std::vector<float> buf(kBlock * kCh, 0.0f);
    auto render = [&]() {
        std::fill(buf.begin(), buf.end(), 0.0f);
        engine.RenderOffline(buf.data(), kBlock);
    };

    // 2) Bus routing + per-bus volume: a voice on SFX is silenced by SFX volume 0.
    {
        const EventId ev = BusEvent(engine, snd, sfx, 0.5f);
        engine.PlayEvent(ev);
        render();
        Check(Energy(buf) > 1.0, "voice on SFX is audible");
        engine.SetBusVolume(sfx, 0.0f);
        render(); // first block ramps the gain to 0 (de-zipper)
        render();
        Check(Energy(buf) < 1e-6, "SFX volume 0 silences it (after gain ramp)");
        engine.SetBusVolume(sfx, 1.0f);
        engine.StopAll();
    }

    // 3) Mute.
    {
        const EventId ev = BusEvent(engine, snd, sfx, 0.5f);
        engine.PlayEvent(ev);
        engine.SetBusMuted(sfx, true);
        render(); // first block ramps to 0 (de-zipper)
        render();
        Check(Energy(buf) < 1e-6, "muted bus is silent (after gain ramp)");
        engine.SetBusMuted(sfx, false);
        engine.StopAll();
    }

    // 4) Solo: soloing Music silences SFX.
    {
        engine.PlayEvent(BusEvent(engine, snd, music, 0.5f));
        engine.PlayEvent(BusEvent(engine, snd, sfx, 0.5f));
        render();
        render(); // settle both bus gains after any prior ramp
        const double both = Energy(buf);
        engine.SetBusSoloed(music, true);
        render();
        render(); // let the solo gain ramp settle
        const double musicOnly = Energy(buf);
        Check(musicOnly > 1.0, "solo: soloed bus still audible");
        Check(musicOnly < both * 0.75, "solo: non-soloed bus silenced");
        engine.SetBusSoloed(music, false);
        engine.StopAll();
    }

    // 5) Metering: a bus with a playing voice reads a non-zero peak; a silent bus reads 0.
    {
        engine.PlayEvent(BusEvent(engine, snd, music, 0.5f));
        render();
        Check(engine.BusMeter(music) > 0.01f, "playing bus meters non-zero");
        Check(engine.BusMeter(sfx) < 1e-4f, "silent bus meters zero");
        engine.StopAll();
    }

    // 6) Sidechain ducking: Dialogue ducks Music.
    {
        engine.PlayEvent(BusEvent(engine, snd, music, 0.5f));
        for (int i = 0; i < 5; ++i) render();
        const float musicAlone = engine.BusMeter(music);

        engine.PlayEvent(BusEvent(engine, snd, dialogue, 0.5f));
        engine.SetDuck(music, dialogue, 0.01f, 0.8f, 5.0f, 200.0f);
        for (int i = 0; i < 40; ++i) render(); // let the duck settle
        const float musicDucked = engine.BusMeter(music);

        Check(musicDucked < musicAlone * 0.5f, "ducking pulls the music bus down under dialogue");
        engine.ClearDuck(music);
        engine.StopAll();
    }

    // 7) Snapshot capture/apply.
    {
        engine.SetBusVolume(sfx, 0.3f);
        engine.CaptureSnapshot("snap");
        engine.SetBusVolume(sfx, 0.9f);
        Check(std::fabs(engine.BusVolume(sfx) - 0.9f) < 1e-4f, "volume changed before apply");
        Check(engine.ApplySnapshot("snap"), "apply snapshot");
        Check(std::fabs(engine.BusVolume(sfx) - 0.3f) < 1e-4f, "snapshot restored volume");
        Check(!engine.ApplySnapshot("nope"), "unknown snapshot returns false");
        engine.SetBusVolume(sfx, 1.0f);
    }

    // 8) Send: A -> B routes signal into B.
    {
        const BusId a = engine.CreateBus("A", master);
        const BusId b = engine.CreateBus("B", master);
        engine.AddSend(a, b, 1.0f);
        engine.PlayEvent(BusEvent(engine, snd, a, 0.5f));
        render();
        Check(engine.BusMeter(b) > 0.01f, "send routes signal into the destination bus");
        engine.StopAll();
    }

    engine.Shutdown();

    // 9) C ABI bus smoke.
    {
        reverie_engine* e = reverie_create();
        reverie_config cfg;
        reverie_default_config(&cfg);
        cfg.backend = REVERIE_BACKEND_NULL;
        reverie_init(e, &cfg);
        Check(reverie_master_bus(e) != 0, "C ABI: master bus");
        const reverie_bus g = reverie_create_bus(e, "Guns", 0);
        Check(g != 0 && reverie_find_bus(e, "Guns") == g, "C ABI: create/find bus");
        reverie_set_bus_volume(e, g, 0.25f);
        Check(std::fabs(reverie_get_bus_volume(e, g) - 0.25f) < 1e-4f, "C ABI: bus volume");
        reverie_shutdown(e);
        reverie_destroy(e);
    }

    if (g_failures == 0) {
        std::printf("reverie mixer tests: PASS\n");
        return 0;
    }
    std::printf("reverie mixer tests: FAIL (%d)\n", g_failures);
    return 1;
}
