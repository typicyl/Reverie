// Reverie/SDK/CAPI/reverie_c.cpp - the flat C ABI, implemented over reverie::Engine.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "reverie.h"

#include "Reverie/Reverie.h"

#include <new> // std::nothrow

namespace {
reverie::Engine* Cast(reverie_engine* e) { return reinterpret_cast<reverie::Engine*>(e); }
reverie_result MapResult(reverie::Result r) { return static_cast<reverie_result>(r); }
} // namespace

extern "C" {

reverie_config reverie_default_config(void) {
    reverie_config c;
    c.backend = REVERIE_BACKEND_MINIAUDIO;
    c.sample_rate = 48000;
    c.channels = 2;
    c.period_frames = 0;
    return c;
}

reverie_engine* reverie_create(void) {
    return reinterpret_cast<reverie_engine*>(new (std::nothrow) reverie::Engine());
}

void reverie_destroy(reverie_engine* engine) { delete Cast(engine); }

reverie_result reverie_init(reverie_engine* engine, const reverie_config* config) {
    if (engine == nullptr || config == nullptr) return REVERIE_INVALID_ARGUMENT;
    reverie::Config cfg;
    cfg.backend = config->backend == REVERIE_BACKEND_NULL ? reverie::Backend::Null
                                                          : reverie::Backend::Miniaudio;
    cfg.sampleRate = config->sample_rate;
    cfg.channels = config->channels;
    cfg.periodFrames = config->period_frames;
    return MapResult(Cast(engine)->Init(cfg));
}

void reverie_shutdown(reverie_engine* engine) {
    if (engine != nullptr) Cast(engine)->Shutdown();
}

reverie_result reverie_start(reverie_engine* engine) {
    if (engine == nullptr) return REVERIE_INVALID_ARGUMENT;
    return MapResult(Cast(engine)->Start());
}

reverie_result reverie_stop(reverie_engine* engine) {
    if (engine == nullptr) return REVERIE_INVALID_ARGUMENT;
    return MapResult(Cast(engine)->Stop());
}

void reverie_set_master_volume(reverie_engine* engine, float volume) {
    if (engine != nullptr) Cast(engine)->SetMasterVolume(volume);
}

float reverie_get_master_volume(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->MasterVolume() : 0.0f;
}

unsigned int reverie_output_channels(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->OutputChannels() : 0u;
}

unsigned int reverie_output_sample_rate(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->OutputSampleRate() : 0u;
}

reverie_sound reverie_load_sound_file(reverie_engine* engine, const char* path) {
    return engine != nullptr ? Cast(engine)->LoadSoundFile(path) : 0u;
}

reverie_sound reverie_load_sound_pcm(reverie_engine* engine, const float* interleaved,
                                     unsigned int frame_count, unsigned int channels,
                                     unsigned int sample_rate) {
    return engine != nullptr
               ? Cast(engine)->LoadSoundPCM(interleaved, frame_count, channels, sample_rate)
               : 0u;
}

void reverie_unload_sound(reverie_engine* engine, reverie_sound sound) {
    if (engine != nullptr) Cast(engine)->UnloadSound(sound);
}

reverie_voice reverie_play(reverie_engine* engine, reverie_sound sound, float volume, int loop) {
    return engine != nullptr ? Cast(engine)->Play(sound, volume, loop != 0) : 0u;
}

void reverie_stop_voice(reverie_engine* engine, reverie_voice voice) {
    if (engine != nullptr) Cast(engine)->StopVoice(voice);
}

void reverie_stop_all(reverie_engine* engine) {
    if (engine != nullptr) Cast(engine)->StopAll();
}

unsigned int reverie_active_voice_count(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->ActiveVoiceCount() : 0u;
}

unsigned int reverie_render_offline(reverie_engine* engine, float* out, unsigned int frame_count) {
    return engine != nullptr ? Cast(engine)->RenderOffline(out, frame_count) : 0u;
}

} // extern "C"
