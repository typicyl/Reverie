// Reverie/Tests/ConcurrencyTests.cpp - stresses the lock-free control<->audio boundary.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The single-threaded suites verify behavior; this one verifies the THREADING model of the voice
// pool (Voice.h / VoiceManager). One "audio" thread pulls the render (RenderOffline -> the same
// MixToBuses path a real device drives on its own thread) in a tight loop, while the single control
// thread hammers Play / StopVoice / PlaySpatial / SetMaxVoices. The design contract is: one control
// thread + one audio thread, no lock on the render path. A broken lock-free design shows up here as
// a crash, a hang, or corrupted state. This is most powerful under a thread sanitizer (see the CI
// plan in Docs/ArchitectureAssessment.md); even without one, the high iteration count reliably
// surfaces use-after-free / torn-state bugs. Deterministic assertions run only when quiescent.
#include "Reverie/Reverie.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

using namespace reverie;

static int g_failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

static std::vector<f32> MakeSine(u32 frames, u32 channels) {
    std::vector<f32> s(static_cast<usize>(frames) * channels, 0.0f);
    for (u32 i = 0; i < frames; ++i) {
        const f32 v = 0.25f * std::sin(2.0f * 3.14159265f * 440.0f * (f32)i / 48000.0f);
        for (u32 c = 0; c < channels; ++c) s[(usize)i * channels + c] = v;
    }
    return s;
}

int main() {
    std::printf("reverie concurrency tests\n");

    Engine engine;
    Config cfg;
    cfg.backend = Backend::Null;
    cfg.sampleRate = 48000;
    cfg.channels = 2;
    cfg.maxVoices = 16;
    Check(Succeeded(engine.Init(cfg)), "init(Null)");

    // Short sounds so non-looping voices FINISH mid-storm (exercises the audio-thread teardown
    // racing control-thread Stop on the same slot - the CAS Playing->Stopping path).
    const std::vector<f32> shortSine = MakeSine(512, 1);
    const SoundId snd = engine.LoadSoundPCM(shortSine.data(), 512, 1, 48000);
    Check(snd != kInvalidId, "load short sound");

    constexpr u32 kBlock = 256;
    std::atomic<bool> stop{false};
    std::atomic<u64> blocksRendered{0};

    // Audio thread: pull the graph continuously, like a device callback.
    std::thread audio([&] {
        std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);
        while (!stop.load(std::memory_order_relaxed)) {
            engine.RenderOffline(buf.data(), kBlock);
            blocksRendered.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Control thread (this one): hammer voice operations. IDs are recycled through a small ring so
    // some Stops hit live voices, some hit already-finished ones (both must be safe).
    std::mt19937 rng(0xC0FFEEu);
    std::vector<VoiceId> recent;
    recent.reserve(64);
    constexpr int kIters = 300000;
    for (int i = 0; i < kIters; ++i) {
        const int op = static_cast<int>(rng() % 5u);
        if (op == 0 || op == 1) {
            const bool loop = (rng() % 8u) == 0; // a few looping voices linger
            const VoiceId v = engine.Play(snd, 0.5f, loop);
            if (v != kInvalidId) {
                if (recent.size() < 64)
                    recent.push_back(v);
                else
                    recent[rng() % recent.size()] = v;
            }
        } else if (op == 2) {
            const Float3 p{(f32)((rng() % 200) - 100), 0.0f, (f32)((rng() % 200) - 100)};
            engine.PlaySpatial(snd, p, 0.5f, false);
        } else if (op == 3) {
            if (!recent.empty()) engine.StopVoice(recent[rng() % recent.size()]);
        } else {
            engine.SetMaxVoices(1u + (rng() % 16u));
        }
        // Occasionally read counts from the control thread (must never crash / never exceed sanity).
        // NOTE: active/real/virtual are three separate scans; the audio thread can finish a voice
        // between them, so real+virtual==active is only guaranteed when quiescent (checked below).
        if ((i & 0x3FFF) == 0) {
            Check(engine.ActiveVoiceCount() <= 256u /* pool floor = maxVoices*4 */,
                  "active count within pool bound");
        }
    }

    stop.store(true, std::memory_order_relaxed);
    audio.join();

    Check(blocksRendered.load() > 0, "audio thread rendered blocks during the storm");

    // Quiescent now (audio thread joined): the three counts are measured against a stable pool, so
    // the split must be internally consistent.
    Check(engine.RealVoiceCount() + engine.VirtualVoiceCount() == engine.ActiveVoiceCount(),
          "real + virtual == active (consistent split when quiescent)");

    // Quiescent: stop everything, drain a few blocks so the audio-thread teardown completes.
    engine.StopAll();
    std::vector<f32> buf(static_cast<usize>(kBlock) * 2, 0.0f);
    for (int k = 0; k < 16; ++k) engine.RenderOffline(buf.data(), kBlock);
    Check(engine.ActiveVoiceCount() == 0, "all voices drained after StopAll + render");

    // The engine still works after the storm: play one voice, render, expect non-silent output.
    engine.Play(snd, 1.0f, false);
    std::fill(buf.begin(), buf.end(), 0.0f);
    engine.RenderOffline(buf.data(), kBlock);
    double energy = 0.0;
    for (float x : buf) energy += (double)x * x;
    Check(energy > 0.0, "engine still produces audio after the concurrency storm");

    engine.Shutdown();

    if (g_failures == 0) {
        std::printf("reverie concurrency tests: PASS\n");
        return 0;
    }
    std::printf("reverie concurrency tests: FAIL (%d)\n", g_failures);
    return 1;
}
