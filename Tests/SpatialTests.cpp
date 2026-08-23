// Reverie/Tests/SpatialTests.cpp - Phase 4 deterministic headless test.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Proves the spatial layer (panning backend): a source to the right is louder on the right, to
// the left louder on the left, in front is centred; distance attenuates; moving the source and
// rotating the listener change the pan; spatial events play. Headless / Null backend. Also
// smoke-tests the C ABI spatial surface.
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

double Energy(const std::vector<float>& buf, unsigned channel, unsigned channels) {
    double e = 0.0;
    for (std::size_t f = channel; f < buf.size(); f += channels) e += double(buf[f]) * buf[f];
    return e;
}

} // namespace

int main() {
    using namespace reverie;

    const unsigned kSr = 48000, kCh = 2, kBlock = 512;
    const std::vector<float> sine = MakeSine(2048, kSr, 440.0f, 0.5f);

    Engine engine;
    Check(engine.Init({Backend::Null, kSr, kCh, 0, 64}) == Result::Ok, "Init");
    const SoundId snd = engine.LoadSoundPCM(sine.data(), (unsigned)sine.size(), 1, kSr);
    Check(engine.SpatialBus() != kInvalidId, "Spatial bus exists");

    std::vector<float> buf(kBlock * kCh, 0.0f);
    auto render = [&]() {
        std::fill(buf.begin(), buf.end(), 0.0f);
        engine.RenderOffline(buf.data(), kBlock);
    };

    engine.SetListener(Float3{0, 0, 0}, Float3{0, 0, -1}, Float3{0, 1, 0});

    // 1) Source to the RIGHT -> right channel louder.
    {
        engine.PlaySpatial(snd, Float3{5, 0, 0}, 1.0f, true);
        render();
        Check(Energy(buf, 1, kCh) > Energy(buf, 0, kCh) * 2.0, "source on the right -> right louder");
        engine.StopAll();
    }

    // 2) Source to the LEFT -> left channel louder.
    {
        engine.PlaySpatial(snd, Float3{-5, 0, 0}, 1.0f, true);
        render();
        Check(Energy(buf, 0, kCh) > Energy(buf, 1, kCh) * 2.0, "source on the left -> left louder");
        engine.StopAll();
    }

    // 3) Source in FRONT -> centred (L ~= R).
    {
        engine.PlaySpatial(snd, Float3{0, 0, -5}, 1.0f, true);
        render();
        const double l = Energy(buf, 0, kCh), r = Energy(buf, 1, kCh);
        Check(l > 1.0 && r > 1.0 && std::fabs(l - r) < l * 0.1, "source in front -> centred");
        engine.StopAll();
    }

    // 4) Distance attenuation: far (beyond max distance) is silent, near is loud.
    {
        engine.PlaySpatial(snd, Float3{500, 0, 0}, 1.0f, true); // default maxDistance 100
        render();
        Check(Energy(buf, 0, kCh) + Energy(buf, 1, kCh) < 1e-4, "beyond max distance -> silent");
        engine.StopAll();
        engine.PlaySpatial(snd, Float3{1, 0, 0}, 1.0f, true);
        render();
        Check(Energy(buf, 0, kCh) + Energy(buf, 1, kCh) > 1.0, "within min distance -> audible");
        engine.StopAll();
    }

    // 5) SetVoicePosition moves the source (right -> left).
    {
        const VoiceId v = engine.PlaySpatial(snd, Float3{5, 0, 0}, 1.0f, true);
        render();
        Check(Energy(buf, 1, kCh) > Energy(buf, 0, kCh), "starts on the right");
        engine.SetVoicePosition(v, Float3{-5, 0, 0});
        render();
        Check(Energy(buf, 0, kCh) > Energy(buf, 1, kCh), "moved to the left");
        engine.StopAll();
    }

    // 6) Listener rotation: facing the source centres it.
    {
        engine.PlaySpatial(snd, Float3{5, 0, 0}, 1.0f, true);
        engine.SetListener(Float3{0, 0, 0}, Float3{0, 0, -1}, Float3{0, 1, 0});
        render();
        Check(Energy(buf, 1, kCh) > Energy(buf, 0, kCh) * 2.0, "facing -Z: source on right is right-loud");
        engine.SetListener(Float3{0, 0, 0}, Float3{1, 0, 0}, Float3{0, 1, 0}); // face +X toward source
        render();
        const double l = Energy(buf, 0, kCh), r = Energy(buf, 1, kCh);
        Check(std::fabs(l - r) < std::max(l, r) * 0.2, "facing the source centres it");
        engine.StopAll();
        engine.SetListener(Float3{0, 0, 0}, Float3{0, 0, -1}, Float3{0, 1, 0});
    }

    // 7) Spatial event.
    {
        EventLayerDesc lay;
        lay.pool.push_back({snd, 1.0f});
        lay.loop = true;
        EventDesc def;
        def.layers.push_back(lay);
        const EventId ev = engine.RegisterEvent(def);
        Check(engine.PlayEventAt(ev, Float3{5, 0, 0}, 1.0f) != 0, "spatial event plays");
        render();
        Check(Energy(buf, 1, kCh) > Energy(buf, 0, kCh), "spatial event pans right");
        engine.StopAll();
    }

    engine.Shutdown();

    // 8) C ABI spatial smoke.
    {
        reverie_engine* e = reverie_create();
        reverie_config cfg;
        reverie_default_config(&cfg);
        cfg.backend = REVERIE_BACKEND_NULL;
        reverie_init(e, &cfg);
        const reverie_sound s = reverie_load_sound_pcm(e, sine.data(), (unsigned)sine.size(), 1, kSr);
        reverie_set_listener(e, 0, 0, 0, 0, 0, -1, 0, 1, 0);
        Check(reverie_play_spatial(e, s, 5, 0, 0, 1.0f, 1) != 0, "C ABI: play spatial");
        std::vector<float> b(kBlock * kCh, 0.0f);
        reverie_render_offline(e, b.data(), kBlock);
        Check(Energy(b, 1, kCh) > Energy(b, 0, kCh), "C ABI: spatial pans right");
        reverie_shutdown(e);
        reverie_destroy(e);
    }

    if (g_failures == 0) {
        std::printf("reverie spatial tests: PASS\n");
        return 0;
    }
    std::printf("reverie spatial tests: FAIL (%d)\n", g_failures);
    return 1;
}
