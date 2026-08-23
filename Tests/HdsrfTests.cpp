// Reverie/Tests/HdsrfTests.cpp - the .HDSRF cooked audio container (codec + engine load).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Cooks f32 PCM into an .HDSRF blob and verifies: the header + seek table decode, the blob is
// smaller than the f32 source (int16 payload), a full decode round-trips within int16 tolerance,
// a single chunk decodes correctly, corrupt/truncated/foreign blobs are rejected, and the engine
// loads a cooked blob into a playable sound. Uses the runtime codec directly + the SDK.
#include "Serialization/Hdsrf.h" // runtime codec (internal header)

#include "Reverie/Reverie.h" // SDK

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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

int main() {
    std::printf("reverie hdsrf tests\n");

    constexpr u32 kSr = 48000;
    constexpr u32 kCh = 2;
    constexpr u32 kFrames = 1000;
    constexpr u32 kChunk = 256;

    std::vector<f32> pcm(static_cast<usize>(kFrames) * kCh, 0.0f);
    for (u32 i = 0; i < kFrames; ++i) {
        const f32 v = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * (f32)i / (f32)kSr);
        pcm[(usize)i * kCh + 0] = v;
        pcm[(usize)i * kCh + 1] = -v; // distinct channels
    }

    std::vector<u8> blob;
    Check(Succeeded(CookHdsrf(pcm.data(), kFrames, kCh, kSr, kChunk, blob)), "cook hdsrf");
    Check(blob.size() < static_cast<usize>(kFrames) * kCh * sizeof(f32),
          "int16 payload is smaller than the f32 source");

    // Header + seek table.
    HdsrfHeader h;
    Check(Succeeded(ReadHdsrfHeader(blob.data(), blob.size(), h)), "read header");
    Check(h.version == kHdsrfVersion && h.sampleRate == kSr && h.channels == kCh, "header fields");
    Check(h.totalFrames == kFrames && h.chunkFrames == kChunk, "header frame counts");
    Check(h.chunkCount == (kFrames + kChunk - 1) / kChunk, "chunk count");
    Check(h.chunkOffsets.size() == h.chunkCount, "seek table size matches chunk count");

    // Full decode round-trip within int16 tolerance.
    AudioBuffer buf;
    Check(Succeeded(ReadHdsrf(blob.data(), blob.size(), buf)), "read full");
    Check(buf.channels == kCh && buf.sampleRate == kSr, "decoded format");
    Check(buf.samples.size() == static_cast<usize>(kFrames) * kCh, "decoded sample count");
    double maxErr = 0.0;
    for (usize i = 0; i < buf.samples.size(); ++i)
        maxErr = std::max(maxErr, std::fabs((double)buf.samples[i] - (double)pcm[i]));
    Check(maxErr < 1.0 / 32000.0, "round-trip within 16-bit tolerance");

    // Single-chunk decode (chunk 0 is full, last chunk is partial).
    std::vector<f32> chunk(static_cast<usize>(kChunk) * kCh, 0.0f);
    u32 n = 0;
    Check(Succeeded(DecodeHdsrfChunk(blob.data(), blob.size(), h, 0, chunk.data(), kChunk, &n)),
          "decode chunk 0");
    Check(n == kChunk, "chunk 0 is full");
    Check(std::fabs((double)chunk[0] - (double)pcm[0]) < 1.0 / 32000.0, "chunk 0 sample matches");
    const u32 last = h.chunkCount - 1;
    Check(Succeeded(DecodeHdsrfChunk(blob.data(), blob.size(), h, last, chunk.data(), kChunk, &n)),
          "decode last chunk");
    Check(n == kFrames - last * kChunk, "last chunk has the remainder frames");

    // Streaming reader: incremental reads reproduce the full decode with bounded memory (one chunk).
    {
        HdsrfStream stream;
        Check(Succeeded(stream.Open(blob.data(), blob.size())), "stream: open");
        Check(stream.Channels() == kCh && stream.TotalFrames() == kFrames, "stream: header");
        std::vector<f32> acc;
        acc.reserve(static_cast<usize>(kFrames) * kCh);
        std::vector<f32> tmp(static_cast<usize>(37) * kCh, 0.0f); // odd size crosses chunk edges
        for (;;) {
            const u32 got = stream.Read(tmp.data(), 37, /*loop*/ false);
            if (got == 0) break;
            acc.insert(acc.end(), tmp.begin(), tmp.begin() + static_cast<usize>(got) * kCh);
        }
        Check(acc.size() == static_cast<usize>(kFrames) * kCh, "stream: read every frame once");
        bool eq = acc.size() == buf.samples.size();
        for (usize i = 0; eq && i < acc.size(); ++i)
            if (acc[i] != buf.samples[i]) eq = false;
        Check(eq, "stream: incremental reads match the full decode exactly");
        Check(stream.AtEnd(), "stream: at end after reading all");
        stream.Seek(0);
        Check(stream.Read(tmp.data(), 37, /*loop*/ true) == 37, "stream: loop read wraps");
    }

    // Validation.
    std::vector<u8> garbage(40, 0x7E);
    Check(Failed(ReadHdsrf(garbage.data(), garbage.size(), buf)), "reject: bad magic");
    std::vector<u8> truncated(blob.begin(), blob.begin() + (blob.size() / 2));
    Check(Failed(ReadHdsrf(truncated.data(), truncated.size(), buf)), "reject: truncated");
    Check(Failed(ReadHdsrfHeader(blob.data(), 4, h)), "reject: too small for header");

    // Engine load path (SDK).
    {
        Engine e;
        Config cfg;
        cfg.backend = Backend::Null;
        cfg.sampleRate = kSr;
        cfg.channels = 2;
        Check(Succeeded(e.Init(cfg)), "engine init");
        const SoundId s = e.LoadSoundHdsrf(blob.data(), blob.size());
        Check(s != kInvalidId, "engine loads cooked blob");
        Check(e.LoadSoundHdsrf(garbage.data(), garbage.size()) == kInvalidId,
              "engine rejects garbage blob");
        e.Play(s, 1.0f, false);
        std::vector<f32> out(512 * 2, 0.0f);
        e.RenderOffline(out.data(), 512);
        double energy = 0.0;
        for (float x : out) energy += (double)x * x;
        Check(energy > 0.0, "cooked sound plays");
        e.Shutdown();
    }

    // Async load of the cooked blob (background worker + poll).
    {
        Engine e;
        Config cfg;
        cfg.backend = Backend::Null;
        cfg.sampleRate = kSr;
        cfg.channels = 2;
        Check(Succeeded(e.Init(cfg)), "async: init");
        const u32 req = e.LoadSoundHdsrfAsync(blob.data(), blob.size());
        Check(req != 0, "async: got a request id");
        SoundId s = kInvalidId;
        bool done = false;
        for (int i = 0; i < 2000 && !done; ++i) {
            done = e.PollLoad(req, s);
            if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(done, "async: load completed");
        Check(s != kInvalidId, "async: produced a valid sound");
        Check(!e.PollLoad(req, s), "async: result consumed (second poll is false)");
        e.Shutdown();
    }

    // Streaming playback: decode in the background into a ring, mix on the audio thread.
    {
        Engine e;
        Config cfg;
        cfg.backend = Backend::Null;
        cfg.sampleRate = kSr;
        cfg.channels = 2;
        Check(Succeeded(e.Init(cfg)), "stream: init");
        const VoiceId sv = e.PlayStream(blob.data(), blob.size(), 1.0f, /*loop*/ false, kInvalidId);
        Check(sv != kInvalidId, "stream: play returns a voice");
        std::vector<f32> out(512 * 2, 0.0f);
        double energy = 0.0;
        for (int i = 0; i < 300; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // let the decoder fill the ring
            std::fill(out.begin(), out.end(), 0.0f);
            e.RenderOffline(out.data(), 512);
            for (float x : out) energy += (double)x * x;
        }
        Check(energy > 0.0, "stream: produced audio");
        Check(e.ActiveVoiceCount() == 0, "stream: voice ended after a short non-looping stream drained");
        e.Shutdown();
    }

    if (g_failures == 0) {
        std::printf("reverie hdsrf tests: PASS\n");
        return 0;
    }
    std::printf("reverie hdsrf tests: FAIL (%d)\n", g_failures);
    return 1;
}
