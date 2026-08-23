// Reverie/Tools/Cook/main.cpp - reverie-cook: source audio -> .HDSRF cooked runtime asset.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The offline cook step of the asset pipeline: decode a source file (wav/flac/mp3/ogg via the
// runtime's miniaudio decoder) into f32 PCM, then cook it into the versioned, chunked, seek-table
// .HDSRF container the runtime loads. Usage:
//     reverie-cook <input-audio> <output.hdsrf> [chunkFrames]
//     reverie-cook --selftest              (in-memory cook+verify; no files needed, for CI)
#include "Audio/AudioBuffer.h"
#include "Audio/AudioDecoder.h"
#include "Serialization/Hdsrf.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace reverie;

static int SelfTest() {
    // Cook a synthetic tone in memory and read it back, checking the round-trip.
    const u32 sr = 48000, ch = 2, frames = 4096;
    std::vector<f32> pcm(static_cast<usize>(frames) * ch);
    for (u32 i = 0; i < frames; ++i) {
        const f32 v = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * (f32)i / (f32)sr);
        pcm[(usize)i * ch + 0] = v;
        pcm[(usize)i * ch + 1] = -v;
    }
    std::vector<u8> blob;
    if (Failed(CookHdsrf(pcm.data(), frames, ch, sr, 0, blob))) {
        std::printf("reverie-cook selftest: FAIL (cook)\n");
        return 1;
    }
    AudioBuffer back;
    if (Failed(ReadHdsrf(blob.data(), blob.size(), back)) || back.channels != ch ||
        back.FrameCount() != frames) {
        std::printf("reverie-cook selftest: FAIL (readback)\n");
        return 1;
    }
    std::printf("reverie-cook selftest: PASS (%u frames, %u ch -> %zu bytes)\n", frames, ch,
                blob.size());
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--selftest") return SelfTest();
    if (argc < 3) {
        std::printf("usage: reverie-cook <input-audio> <output.hdsrf> [chunkFrames]\n"
                    "       reverie-cook --selftest\n");
        return 2;
    }
    const char* inPath = argv[1];
    const char* outPath = argv[2];
    const u32 chunkFrames = argc >= 4 ? static_cast<u32>(std::atoi(argv[3])) : 0;

    AudioBuffer buffer;
    if (Failed(AudioDecoder::DecodeFile(inPath, buffer)) || buffer.channels == 0) {
        std::printf("reverie-cook: failed to decode '%s'\n", inPath);
        return 1;
    }
    const u32 frames = buffer.FrameCount();
    std::vector<u8> blob;
    if (Failed(CookHdsrf(buffer.samples.data(), frames, buffer.channels, buffer.sampleRate,
                         chunkFrames, blob))) {
        std::printf("reverie-cook: cook failed\n");
        return 1;
    }
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::printf("reverie-cook: cannot open output '%s'\n", outPath);
        return 1;
    }
    out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    out.close();

    const usize sourceBytes = static_cast<usize>(frames) * buffer.channels * sizeof(f32);
    std::printf("reverie-cook: '%s' -> '%s'\n  %u frames, %u ch, %u Hz\n  %zu bytes (%.0f%% of f32 source)\n",
                inPath, outPath, frames, buffer.channels, buffer.sampleRate, blob.size(),
                sourceBytes > 0 ? 100.0 * (double)blob.size() / (double)sourceBytes : 0.0);
    return 0;
}
