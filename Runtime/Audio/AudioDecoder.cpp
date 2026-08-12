// Reverie/Runtime/Audio/AudioDecoder.cpp - see AudioDecoder.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// miniaudio's ma_decoder (WAV/FLAC/MP3) is used here, and ONLY here (plus the impl TU). It is
// configured to output f32 at the source's native rate/channels; Reverie resamples/upmixes
// later in the voice mixer.
#include "Audio/AudioDecoder.h"

#include "Core/Log.h"

#include "miniaudio.h"

#include <cstring>
#include <vector>

namespace reverie {

namespace {

// Reads every remaining frame from an f32-configured decoder into `out`.
Result DrainDecoder(ma_decoder& dec, AudioBuffer& out) {
    const u32 channels = dec.outputChannels;
    out.channels = channels;
    out.sampleRate = dec.outputSampleRate;
    out.samples.clear();
    if (channels == 0) return Result::DecodeError;

    // Reserve from the reported length when available (WAV/FLAC); MP3 may report 0.
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &total) == MA_SUCCESS && total > 0)
        out.samples.reserve(static_cast<usize>(total) * channels);

    constexpr ma_uint64 kChunkFrames = 4096;
    std::vector<f32> chunk(static_cast<usize>(kChunkFrames) * channels);
    for (;;) {
        ma_uint64 read = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&dec, chunk.data(), kChunkFrames, &read);
        if (read > 0) {
            out.samples.insert(out.samples.end(), chunk.begin(),
                               chunk.begin() + static_cast<usize>(read) * channels);
        }
        if (r != MA_SUCCESS || read == 0) break; // MA_AT_END or error
    }
    return out.samples.empty() ? Result::DecodeError : Result::Ok;
}

} // namespace

Result AudioDecoder::DecodeFile(const char* path, AudioBuffer& out) {
    if (path == nullptr) return Result::InvalidArgument;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0); // native ch/rate
    ma_decoder dec;
    if (ma_decoder_init_file(path, &cfg, &dec) != MA_SUCCESS) {
        LogFormat(LogLevel::Error, "AudioDecoder: cannot open '%s'", path);
        return Result::FileNotFound;
    }
    const Result r = DrainDecoder(dec, out);
    ma_decoder_uninit(&dec);
    return r;
}

Result AudioDecoder::DecodeMemory(const void* data, usize bytes, AudioBuffer& out) {
    if (data == nullptr || bytes == 0) return Result::InvalidArgument;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder dec;
    if (ma_decoder_init_memory(data, bytes, &cfg, &dec) != MA_SUCCESS)
        return Result::DecodeError;
    const Result r = DrainDecoder(dec, out);
    ma_decoder_uninit(&dec);
    return r;
}

// ---------------------------------------------------------------------------
// AudioStream
// ---------------------------------------------------------------------------
struct AudioStream::Impl {
    ma_decoder decoder{};
    bool open = false;
    std::vector<u8> ownedBytes; // for OpenMemory: ma_decoder_init_memory references this
    AudioFormat format;
};

AudioStream::AudioStream() : impl_(std::make_unique<Impl>()) {}
AudioStream::~AudioStream() { Close(); }

Result AudioStream::OpenFile(const char* path) {
    if (path == nullptr) return Result::InvalidArgument;
    Close();
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_file(path, &cfg, &impl_->decoder) != MA_SUCCESS) return Result::FileNotFound;
    impl_->open = true;
    impl_->format = AudioFormat{impl_->decoder.outputChannels, impl_->decoder.outputSampleRate,
                                SampleFormat::F32};
    return Result::Ok;
}

Result AudioStream::OpenMemory(const void* data, usize bytes) {
    if (data == nullptr || bytes == 0) return Result::InvalidArgument;
    Close();
    impl_->ownedBytes.assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + bytes);
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    if (ma_decoder_init_memory(impl_->ownedBytes.data(), impl_->ownedBytes.size(), &cfg,
                               &impl_->decoder) != MA_SUCCESS) {
        impl_->ownedBytes.clear();
        return Result::DecodeError;
    }
    impl_->open = true;
    impl_->format = AudioFormat{impl_->decoder.outputChannels, impl_->decoder.outputSampleRate,
                                SampleFormat::F32};
    return Result::Ok;
}

bool AudioStream::IsOpen() const { return impl_->open; }

void AudioStream::Close() {
    if (impl_->open) {
        ma_decoder_uninit(&impl_->decoder);
        impl_->open = false;
    }
    impl_->ownedBytes.clear();
}

AudioFormat AudioStream::Format() const { return impl_->format; }

u64 AudioStream::FrameCount() const {
    if (!impl_->open) return 0;
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&impl_->decoder, &total) != MA_SUCCESS) return 0;
    return total;
}

u32 AudioStream::ReadFrames(f32* out, u32 frameCount) {
    if (!impl_->open || out == nullptr || frameCount == 0) return 0;
    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&impl_->decoder, out, frameCount, &read);
    return static_cast<u32>(read);
}

Result AudioStream::Seek(u64 frame) {
    if (!impl_->open) return Result::NotInitialized;
    return ma_decoder_seek_to_pcm_frame(&impl_->decoder, frame) == MA_SUCCESS ? Result::Ok
                                                                              : Result::Error;
}

} // namespace reverie
