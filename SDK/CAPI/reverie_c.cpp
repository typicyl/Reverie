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

// The C-ABI event builder is just a heap-allocated runtime EventDesc being assembled.
struct reverie_event_builder {
    reverie::EventDesc desc;
};

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

void reverie_set_max_voices(reverie_engine* engine, unsigned int count) {
    if (engine != nullptr) Cast(engine)->SetMaxVoices(count);
}

void reverie_set_seed(reverie_engine* engine, unsigned long long seed) {
    if (engine != nullptr) Cast(engine)->SetSeed(seed);
}

unsigned int reverie_real_voice_count(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->RealVoiceCount() : 0u;
}

unsigned int reverie_virtual_voice_count(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->VirtualVoiceCount() : 0u;
}

reverie_event_builder* reverie_event_builder_create(int priority, unsigned int max_instances,
                                                    unsigned int concurrency_group) {
    auto* b = new (std::nothrow) reverie_event_builder();
    if (b != nullptr) {
        b->desc.priority = priority;
        b->desc.maxInstances = max_instances;
        b->desc.concurrencyGroup = concurrency_group;
    }
    return b;
}

int reverie_event_builder_add_layer(reverie_event_builder* builder, float volume,
                                    float volume_variance, float pitch, float pitch_variance,
                                    int loop, float probability) {
    if (builder == nullptr) return -1;
    reverie::EventLayerDesc layer;
    layer.volume = volume;
    layer.volumeVariance = volume_variance;
    layer.pitch = pitch;
    layer.pitchVariance = pitch_variance;
    layer.loop = loop != 0;
    layer.probability = probability;
    builder->desc.layers.push_back(std::move(layer));
    return static_cast<int>(builder->desc.layers.size()) - 1;
}

void reverie_event_builder_add_sound(reverie_event_builder* builder, int layer,
                                     reverie_sound sound, float weight) {
    if (builder == nullptr || layer < 0) return;
    if (static_cast<std::size_t>(layer) >= builder->desc.layers.size()) return;
    builder->desc.layers[static_cast<std::size_t>(layer)].pool.push_back(
        reverie::EventPoolEntry{sound, weight});
}

reverie_event reverie_event_builder_register(reverie_engine* engine,
                                             reverie_event_builder* builder) {
    if (engine == nullptr || builder == nullptr) {
        delete builder;
        return 0u;
    }
    const reverie_event id = Cast(engine)->RegisterEvent(builder->desc);
    delete builder;
    return id;
}

void reverie_event_builder_destroy(reverie_event_builder* builder) { delete builder; }

reverie_event_instance reverie_play_event(reverie_engine* engine, reverie_event event,
                                          float volume) {
    return engine != nullptr ? Cast(engine)->PlayEvent(event, volume) : 0u;
}

void reverie_stop_event_instance(reverie_engine* engine, reverie_event_instance instance) {
    if (engine != nullptr) Cast(engine)->StopEventInstance(instance);
}

unsigned int reverie_active_instance_count(reverie_engine* engine, reverie_event event) {
    return engine != nullptr ? Cast(engine)->ActiveInstanceCount(event) : 0u;
}

void reverie_event_builder_set_layer_bus(reverie_event_builder* builder, int layer,
                                         reverie_bus bus) {
    if (builder == nullptr || layer < 0) return;
    if (static_cast<std::size_t>(layer) >= builder->desc.layers.size()) return;
    builder->desc.layers[static_cast<std::size_t>(layer)].bus = bus;
}

reverie_bus reverie_master_bus(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->MasterBus() : 0u;
}

reverie_bus reverie_create_bus(reverie_engine* engine, const char* name, reverie_bus parent) {
    return engine != nullptr ? Cast(engine)->CreateBus(name, parent) : 0u;
}

reverie_bus reverie_find_bus(reverie_engine* engine, const char* name) {
    return engine != nullptr ? Cast(engine)->FindBus(name) : 0u;
}

void reverie_set_bus_volume(reverie_engine* engine, reverie_bus bus, float volume) {
    if (engine != nullptr) Cast(engine)->SetBusVolume(bus, volume);
}

float reverie_get_bus_volume(reverie_engine* engine, reverie_bus bus) {
    return engine != nullptr ? Cast(engine)->BusVolume(bus) : 0.0f;
}

void reverie_set_bus_muted(reverie_engine* engine, reverie_bus bus, int muted) {
    if (engine != nullptr) Cast(engine)->SetBusMuted(bus, muted != 0);
}

void reverie_set_bus_soloed(reverie_engine* engine, reverie_bus bus, int soloed) {
    if (engine != nullptr) Cast(engine)->SetBusSoloed(bus, soloed != 0);
}

void reverie_add_send(reverie_engine* engine, reverie_bus from, reverie_bus to, float level) {
    if (engine != nullptr) Cast(engine)->AddSend(from, to, level);
}

void reverie_set_duck(reverie_engine* engine, reverie_bus ducked, reverie_bus sidechain,
                      float threshold, float amount, float attack_ms, float release_ms) {
    if (engine != nullptr)
        Cast(engine)->SetDuck(ducked, sidechain, threshold, amount, attack_ms, release_ms);
}

void reverie_clear_duck(reverie_engine* engine, reverie_bus ducked) {
    if (engine != nullptr) Cast(engine)->ClearDuck(ducked);
}

float reverie_bus_meter(reverie_engine* engine, reverie_bus bus) {
    return engine != nullptr ? Cast(engine)->BusMeter(bus) : 0.0f;
}

void reverie_capture_snapshot(reverie_engine* engine, const char* name) {
    if (engine != nullptr) Cast(engine)->CaptureSnapshot(name);
}

int reverie_apply_snapshot(reverie_engine* engine, const char* name) {
    return (engine != nullptr && Cast(engine)->ApplySnapshot(name)) ? 1 : 0;
}

} // extern "C"
