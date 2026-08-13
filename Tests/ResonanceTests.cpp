// Reverie/Tests/ResonanceTests.cpp - Phase 4b: the HDS Resonance HRTF backend.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Detects the active spatial backend at runtime: when Reverie was built with the Resonance
// backend (-DREVERIE_WITH_RESONANCE=ON) and it is requested, this drives real HRTF binaural
// rendering through the ISpatialRenderer seam and asserts non-silent output; otherwise it
// SKIPS (and passes), so the same test binary is valid in a panning-only build. Headless.
#include "Reverie/Reverie.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

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

} // namespace

int main() {
    using namespace reverie;

    const unsigned kSr = 48000, kCh = 2, kBlock = 512;

    Engine engine;
    Config cfg;
    cfg.backend = Backend::Null;
    cfg.sampleRate = kSr;
    cfg.channels = kCh;
    cfg.useResonance = true; // request Resonance; falls back to panning if not built
    if (engine.Init(cfg) != Result::Ok) {
        std::printf("reverie resonance tests: FAIL (init)\n");
        return 1;
    }

    const char* backend = engine.SpatialBackendName();
    if (std::strcmp(backend, "Resonance") != 0) {
        std::printf("reverie resonance tests: SKIP (backend=%s; built without Resonance)\n",
                    backend);
        return 0; // panning-only build: nothing to verify here
    }

    // Resonance is active: render a spatial source and confirm real binaural output.
    const std::vector<float> sine = MakeSine(4096, kSr, 440.0f, 0.5f);
    const SoundId snd = engine.LoadSoundPCM(sine.data(), (unsigned)sine.size(), 1, kSr);
    engine.SetListener(Float3{0, 0, 0}, Float3{0, 0, -1}, Float3{0, 1, 0});
    engine.PlaySpatial(snd, Float3{2.0f, 0.0f, 0.0f}, 1.0f, /*loop*/ true);

    // Render several blocks: the renderer re-blocks to Resonance's fixed size and the HRTF/onset
    // ramps up, so measure a later block.
    std::vector<float> buf(kBlock * kCh, 0.0f);
    double energy = 0.0;
    for (int i = 0; i < 12; ++i) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        engine.RenderOffline(buf.data(), kBlock);
        energy = Energy(buf);
    }

    engine.Shutdown();

    if (energy > 1e-4) {
        std::printf("reverie resonance tests: PASS (HRTF binaural output, energy=%.4f)\n", energy);
        return 0;
    }
    std::printf("reverie resonance tests: FAIL (Resonance backend produced silence)\n");
    return 1;
}
