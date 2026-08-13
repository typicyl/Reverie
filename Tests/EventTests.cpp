// Reverie/Tests/EventTests.cpp - Phase 2 deterministic headless test.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Proves the layered event system + voice management: events spawn one voice per triggered
// layer, probability gates layers, a voice budget virtualizes the excess, priority decides who
// stays audible (stealing), and per-event maxInstances caps concurrency. Deterministic via a
// seeded RNG. Runs headless on the Null backend. Also smoke-tests the C ABI event builder.
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

reverie::EventDesc OneLayerEvent(reverie::SoundId snd, float volume, bool loop, int priority,
                                 unsigned maxInstances) {
    reverie::EventLayerDesc layer;
    layer.pool.push_back({snd, 1.0f});
    layer.volume = volume;
    layer.loop = loop;
    reverie::EventDesc def;
    def.layers.push_back(layer);
    def.priority = priority;
    def.maxInstances = maxInstances;
    return def;
}

} // namespace

int main() {
    using namespace reverie;

    const unsigned kSr = 48000, kCh = 2, kBlock = 512;
    const std::vector<float> sine = MakeSine(12000, kSr, 440.0f, 0.3f);

    Engine engine;
    Check(engine.Init({Backend::Null, kSr, kCh, 0, 64}) == Result::Ok, "Init(Null)");
    engine.SetSeed(42);
    const SoundId snd = engine.LoadSoundPCM(sine.data(), (unsigned)sine.size(), 1, kSr);
    Check(snd != kInvalidId, "load sound");

    // 1) A 3-layer event spawns one voice per layer.
    {
        EventDesc def;
        for (int i = 0; i < 3; ++i) {
            EventLayerDesc l;
            l.pool.push_back({snd, 1.0f});
            def.layers.push_back(l);
        }
        const EventId ev = engine.RegisterEvent(def);
        Check(ev != kInvalidId, "register 3-layer event");
        const InstanceId inst = engine.PlayEvent(ev);
        Check(inst != 0, "play event returns instance");
        Check(engine.ActiveVoiceCount() == 3, "3-layer event -> 3 voices");
        Check(engine.ActiveInstanceCount(ev) == 1, "1 active instance");
        engine.StopEventInstance(inst);
        Check(engine.ActiveVoiceCount() == 0, "stop instance -> no voices");
        engine.StopAll();
    }

    // 2) Probability gates a layer (prob 0 never spawns, prob 1 always).
    {
        EventDesc def;
        EventLayerDesc always;
        always.pool.push_back({snd, 1.0f});
        always.probability = 1.0f;
        EventLayerDesc never;
        never.pool.push_back({snd, 1.0f});
        never.probability = 0.0f;
        def.layers = {always, never};
        const EventId ev = engine.RegisterEvent(def);
        engine.PlayEvent(ev);
        Check(engine.ActiveVoiceCount() == 1, "probability: only the certain layer spawns");
        engine.StopAll();
    }

    // 3) Voice budget virtualizes the excess (5 voices, budget 2).
    {
        engine.SetMaxVoices(2);
        for (int i = 0; i < 5; ++i) engine.Play(snd);
        Check(engine.ActiveVoiceCount() == 5, "budget: 5 total voices");
        Check(engine.RealVoiceCount() == 2, "budget: 2 real voices");
        Check(engine.VirtualVoiceCount() == 3, "budget: 3 virtual voices");
        engine.StopAll();
        engine.SetMaxVoices(64);
    }

    // 4) Priority-based stealing: the higher-priority (louder here) voice keeps the one real slot.
    {
        const EventId low = engine.RegisterEvent(OneLayerEvent(snd, 0.1f, true, 0, 0));
        const EventId high = engine.RegisterEvent(OneLayerEvent(snd, 0.9f, true, 10, 0));
        engine.SetMaxVoices(1);

        engine.StopAll();
        engine.PlayEvent(low);
        std::vector<float> lowOnly(kBlock * kCh, 0.0f);
        engine.RenderOffline(lowOnly.data(), kBlock);
        const double eLow = Energy(lowOnly);

        engine.StopAll();
        engine.PlayEvent(low);
        engine.PlayEvent(high); // budget 1 -> high (priority 10) wins the real slot
        Check(engine.RealVoiceCount() == 1, "steal: 1 real voice");
        Check(engine.VirtualVoiceCount() == 1, "steal: 1 virtual voice");
        std::vector<float> withHigh(kBlock * kCh, 0.0f);
        engine.RenderOffline(withHigh.data(), kBlock);
        Check(Energy(withHigh) > eLow * 4.0, "steal: high-priority voice is the audible one");

        engine.StopAll();
        engine.SetMaxVoices(64);
    }

    // 5) Per-event maxInstances caps concurrency (steal oldest).
    {
        const EventId ev = engine.RegisterEvent(OneLayerEvent(snd, 0.5f, true, 0, 2));
        engine.PlayEvent(ev);
        engine.PlayEvent(ev);
        engine.PlayEvent(ev); // third steals the oldest instance
        Check(engine.ActiveInstanceCount(ev) == 2, "maxInstances=2 caps at 2 instances");
        engine.StopAll();
    }

    // 6) Determinism: same seed -> byte-identical render with per-trigger variance.
    {
        EventDesc def;
        EventLayerDesc l;
        l.pool.push_back({snd, 1.0f});
        l.volumeVariance = 0.5f;
        l.pitchVariance = 0.2f;
        def.layers.push_back(l);
        const EventId ev = engine.RegisterEvent(def);

        engine.SetSeed(1234);
        engine.StopAll();
        engine.PlayEvent(ev);
        std::vector<float> a(kBlock * kCh, 0.0f);
        engine.RenderOffline(a.data(), kBlock);

        engine.SetSeed(1234);
        engine.StopAll();
        engine.PlayEvent(ev);
        std::vector<float> b(kBlock * kCh, 0.0f);
        engine.RenderOffline(b.data(), kBlock);

        Check(std::equal(a.begin(), a.end(), b.begin()), "seeded variance is deterministic");
        engine.StopAll();
    }

    engine.Shutdown();

    // 7) C ABI event builder smoke.
    {
        reverie_engine* e = reverie_create();
        reverie_config cfg = reverie_default_config();
        cfg.backend = REVERIE_BACKEND_NULL;
        Check(reverie_init(e, &cfg) == REVERIE_OK, "C ABI: init");
        const reverie_sound s = reverie_load_sound_pcm(e, sine.data(), (unsigned)sine.size(), 1, kSr);
        reverie_event_builder* b = reverie_event_builder_create(0, 0, 0);
        const int layer = reverie_event_builder_add_layer(b, 1.0f, 0.0f, 1.0f, 0.0f, 0, 1.0f);
        reverie_event_builder_add_sound(b, layer, s, 1.0f);
        const reverie_event ev = reverie_event_builder_register(e, b);
        Check(ev != 0, "C ABI: build + register event");
        Check(reverie_play_event(e, ev, 1.0f) != 0, "C ABI: play event");
        Check(reverie_active_instance_count(e, ev) == 1, "C ABI: 1 active instance");
        std::vector<float> buf(kBlock * kCh, 0.0f);
        reverie_render_offline(e, buf.data(), kBlock);
        Check(Energy(buf) > 1.0, "C ABI: event produced audio");
        reverie_shutdown(e);
        reverie_destroy(e);
    }

    if (g_failures == 0) {
        std::printf("reverie event tests: PASS\n");
        return 0;
    }
    std::printf("reverie event tests: FAIL (%d)\n", g_failures);
    return 1;
}
