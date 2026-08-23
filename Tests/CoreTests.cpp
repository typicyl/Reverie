// Reverie/Tests/CoreTests.cpp - Phase 1 deterministic headless test.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Proves the Phase 1 pipeline (PCM -> voice -> mixer -> master -> Null device offline pull)
// end to end WITHOUT an audio device, so it runs in CI. Exercises both the C++ facade and the
// flat C ABI. Deterministic: same inputs -> byte-identical output.
#include "Reverie/Reverie.h"
#include "reverie.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// A mono sine as interleaved f32 at `sampleRate`.
std::vector<float> MakeSine(unsigned frames, unsigned sampleRate, float freq, float amp) {
    std::vector<float> s(frames);
    for (unsigned i = 0; i < frames; ++i)
        s[i] = amp * static_cast<float>(std::sin(2.0 * kPi * freq * i / sampleRate));
    return s;
}

double Energy(const std::vector<float>& buf, unsigned channel, unsigned channels) {
    double e = 0.0;
    for (std::size_t f = channel; f < buf.size(); f += channels) e += double(buf[f]) * buf[f];
    return e;
}

} // namespace

int main() {
    using namespace reverie;

    const unsigned kSr = 48000, kCh = 2, kBlock = 512;
    const std::vector<float> sine = MakeSine(12000, kSr, 440.0f, 0.3f); // 0.25s mono

    // ---- C++ facade ------------------------------------------------------------------
    {
        Engine engine;
        Check(engine.Init({Backend::Null, kSr, kCh, 0}) == Result::Ok, "Init(Null) ok");
        Check(engine.IsInitialized(), "IsInitialized");
        Check(engine.OutputChannels() == kCh, "output channels == 2");
        Check(engine.OutputSampleRate() == kSr, "output rate == 48000");

        const SoundId snd = engine.LoadSoundPCM(sine.data(), (unsigned)sine.size(), 1, kSr);
        Check(snd != kInvalidId, "LoadSoundPCM ok");

        // 1) one voice -> non-silent, mono upmixed equally to L and R.
        std::vector<float> buf(kBlock * kCh, 0.0f);
        Check(engine.Play(snd) != kInvalidId, "Play returns a voice");
        Check(engine.ActiveVoiceCount() == 1, "1 active voice");
        engine.RenderOffline(buf.data(), kBlock);
        const double e1L = Energy(buf, 0, kCh), e1R = Energy(buf, 1, kCh);
        Check(e1L > 1.0, "one voice: left channel non-silent");
        Check(e1R > 1.0, "one voice: right channel non-silent");
        Check(std::fabs(e1L - e1R) < 1e-3, "mono upmix: L == R");

        // 2) master volume 0 -> silence.
        engine.StopAll();
        engine.SetMasterVolume(0.0f);
        engine.Play(snd);
        engine.RenderOffline(buf.data(), kBlock); // first block ramps the gain down to 0 (de-zipper)
        std::fill(buf.begin(), buf.end(), 0.0f);
        engine.RenderOffline(buf.data(), kBlock);
        Check(Energy(buf, 0, kCh) < 1e-9, "master volume 0 -> silence (after gain ramp)");
        engine.SetMasterVolume(1.0f);

        // 3) two voices are louder than one (in-phase identical sources sum).
        engine.StopAll();
        engine.Play(snd);
        std::vector<float> one(kBlock * kCh, 0.0f);
        engine.RenderOffline(one.data(), kBlock);
        engine.StopAll();
        engine.Play(snd);
        engine.Play(snd);
        Check(engine.ActiveVoiceCount() == 2, "2 active voices");
        std::vector<float> two(kBlock * kCh, 0.0f);
        engine.RenderOffline(two.data(), kBlock);
        Check(Energy(two, 0, kCh) > Energy(one, 0, kCh) * 1.5, "two voices louder than one");

        // 4) stop a voice -> becomes inactive.
        engine.StopAll();
        const VoiceId v = engine.Play(snd);
        engine.StopVoice(v);
        std::fill(buf.begin(), buf.end(), 0.0f);
        engine.RenderOffline(buf.data(), kBlock); // reaps the stopped voice
        Check(engine.ActiveVoiceCount() == 0, "stopped voice reaped");
        Check(Energy(buf, 0, kCh) < 1e-9, "stopped voice -> silence");

        // 5) looping voice keeps playing past its buffer length.
        engine.StopAll();
        const std::vector<float> tiny = MakeSine(128, kSr, 440.0f, 0.3f);
        const SoundId loopSnd = engine.LoadSoundPCM(tiny.data(), (unsigned)tiny.size(), 1, kSr);
        engine.Play(loopSnd, 1.0f, /*loop*/ true);
        std::vector<float> longBuf(2048 * kCh, 0.0f); // 16x the 128-frame source
        engine.RenderOffline(longBuf.data(), 2048);
        Check(engine.ActiveVoiceCount() == 1, "looping voice still active");
        Check(Energy(longBuf, 0, kCh) > 1.0, "looping voice still producing audio");

        // 6) determinism: identical setup -> byte-identical output.
        engine.StopAll();
        engine.Play(snd);
        std::vector<float> a(kBlock * kCh, 0.0f);
        engine.RenderOffline(a.data(), kBlock);
        engine.StopAll();
        engine.Play(snd);
        std::vector<float> b(kBlock * kCh, 0.0f);
        engine.RenderOffline(b.data(), kBlock);
        Check(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0, "deterministic output");

        engine.Shutdown();
        Check(!engine.IsInitialized(), "Shutdown clears initialized");
    }

    // ---- Profiling stats snapshot ----------------------------------------------------
    {
        Engine engine;
        Config cfg;
        cfg.backend = Backend::Null;
        cfg.sampleRate = kSr;
        cfg.channels = kCh;
        engine.Init(cfg);
        const SoundId snd = engine.LoadSoundPCM(sine.data(), (u32)sine.size(), 1, kSr);
        engine.Play(snd, 1.0f, true);
        engine.Play(snd, 1.0f, true);
        std::vector<float> b(kBlock * kCh, 0.0f);
        engine.RenderOffline(b.data(), kBlock);
        const EngineStats st = engine.GetStats();
        Check(st.sampleRate == kSr && st.channels == kCh, "stats: format");
        Check(st.activeVoices == 2 && st.realVoices + st.virtualVoices == 2, "stats: voice counts");
        Check(st.cpuLoad >= 0.0f, "stats: cpu load is measured");
        engine.Shutdown();
    }

    // ---- C ABI (proves the flat binding boundary links + runs) -----------------------
    {
        Check(reverie_abi_version() == REVERIE_ABI_VERSION, "C ABI: version matches header");
        Check(reverie_result_string(REVERIE_OK) != nullptr &&
                  reverie_result_string(REVERIE_FILE_NOT_FOUND) != nullptr,
              "C ABI: result_string returns strings");

        reverie_engine* e = reverie_create();
        Check(e != nullptr, "C ABI: create");
        Check(reverie_is_initialized(e) == 0, "C ABI: not initialized before init");
        reverie_config cfg;
        reverie_default_config(&cfg);
        cfg.backend = REVERIE_BACKEND_NULL;
        Check(reverie_init(e, &cfg) == REVERIE_OK, "C ABI: init(Null)");
        Check(reverie_is_initialized(e) == 1, "C ABI: initialized after init");
        const reverie_sound s = reverie_load_sound_pcm(e, sine.data(), (unsigned)sine.size(), 1, kSr);
        Check(s != 0, "C ABI: load pcm");
        Check(reverie_play(e, s, 1.0f, 0) != 0, "C ABI: play");
        std::vector<float> buf(kBlock * kCh, 0.0f);
        reverie_render_offline(e, buf.data(), kBlock);
        Check(Energy(buf, 0, kCh) > 1.0, "C ABI: non-silent output");

        // Bus getters (C ABI is now a superset of the C++ facade).
        const reverie_bus m = reverie_master_bus(e);
        reverie_set_bus_muted(e, m, 1);
        Check(reverie_get_bus_muted(e, m) == 1, "C ABI: get_bus_muted reflects set");
        reverie_set_bus_muted(e, m, 0);
        reverie_set_bus_soloed(e, m, 1);
        Check(reverie_get_bus_soloed(e, m) == 1, "C ABI: get_bus_soloed reflects set");
        reverie_set_bus_soloed(e, m, 0);

        // Event register + unregister round-trip (the previously-missing C-ABI unregister).
        reverie_event_builder* b = reverie_event_builder_create(0, 0, 0);
        const int layer = reverie_event_builder_add_layer(b, 1.0f, 0.0f, 1.0f, 0.0f, 0, 1.0f);
        reverie_event_builder_add_sound(b, layer, s, 1.0f);
        const reverie_event ev = reverie_event_builder_register(e, b);
        Check(ev != 0, "C ABI: register event");
        reverie_unregister_event(e, ev); // links + no crash
        reverie_shutdown(e);
        reverie_destroy(e);
    }

    // ---- max_voices == 0 means the SAME default on both boundaries (was 64 via C, 1 via C++) ----
    {
        Engine engine;
        Config cfg;
        cfg.backend = Backend::Null;
        cfg.maxVoices = 0; // "use default"
        Check(Succeeded(engine.Init(cfg)), "maxVoices=0: init");
        const SoundId snd = engine.LoadSoundPCM(sine.data(), (u32)sine.size(), 1, kSr);
        for (int i = 0; i < 3; ++i) engine.Play(snd);
        Check(engine.RealVoiceCount() == 3, "maxVoices=0 -> generous default (3 voices all real, not 1)");
        engine.Shutdown();
    }

    if (g_failures == 0) {
        std::printf("reverie core tests: PASS\n");
        return 0;
    }
    std::printf("reverie core tests: FAIL (%d)\n", g_failures);
    return 1;
}
