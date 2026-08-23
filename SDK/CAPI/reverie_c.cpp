// Reverie/SDK/CAPI/reverie_c.cpp - the flat C ABI, implemented over reverie::Engine.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "reverie.h"

#include "Reverie/Reverie.h"

#include <cstring> // std::memcpy
#include <new>     // std::nothrow
#include <vector>

namespace {
reverie::Engine* Cast(reverie_engine* e) { return reinterpret_cast<reverie::Engine*>(e); }
reverie_result MapResult(reverie::Result r) { return static_cast<reverie_result>(r); }
} // namespace

// MapResult is a bare static_cast, so the C ABI enum (reverie_result) and the C++ enum
// (reverie::Result) MUST hold identical numeric values. These compile-time guards make a silent
// drift between the two definitions a build error instead of a runtime mis-mapping.
static_assert(static_cast<int>(reverie::Result::Ok) == REVERIE_OK, "result drift: Ok");
static_assert(static_cast<int>(reverie::Result::Error) == REVERIE_ERROR, "result drift: Error");
static_assert(static_cast<int>(reverie::Result::InvalidArgument) == REVERIE_INVALID_ARGUMENT,
              "result drift: InvalidArgument");
static_assert(static_cast<int>(reverie::Result::NotInitialized) == REVERIE_NOT_INITIALIZED,
              "result drift: NotInitialized");
static_assert(static_cast<int>(reverie::Result::AlreadyInitialized) == REVERIE_ALREADY_INITIALIZED,
              "result drift: AlreadyInitialized");
static_assert(static_cast<int>(reverie::Result::DeviceError) == REVERIE_DEVICE_ERROR,
              "result drift: DeviceError");
static_assert(static_cast<int>(reverie::Result::DecodeError) == REVERIE_DECODE_ERROR,
              "result drift: DecodeError");
static_assert(static_cast<int>(reverie::Result::FileNotFound) == REVERIE_FILE_NOT_FOUND,
              "result drift: FileNotFound");
static_assert(static_cast<int>(reverie::Result::Unsupported) == REVERIE_UNSUPPORTED,
              "result drift: Unsupported");
static_assert(static_cast<int>(reverie::Result::OutOfMemory) == REVERIE_OUT_OF_MEMORY,
              "result drift: OutOfMemory");
static_assert(static_cast<int>(reverie::Result::NotFound) == REVERIE_NOT_FOUND,
              "result drift: NotFound");

// Effect/filter enums are cast across the ABI too - keep the C and C++ values locked together.
static_assert(static_cast<int>(reverie::EffectType::Filter) == REVERIE_EFFECT_FILTER, "effect drift");
static_assert(static_cast<int>(reverie::EffectType::Compressor) == REVERIE_EFFECT_COMPRESSOR, "effect drift: Comp");
static_assert(static_cast<int>(reverie::EffectType::Delay) == REVERIE_EFFECT_DELAY, "effect drift: Delay");
static_assert(static_cast<int>(reverie::MusicTransition::Immediate) == REVERIE_MUSIC_IMMEDIATE, "music drift: Imm");
static_assert(static_cast<int>(reverie::MusicTransition::NextBeat) == REVERIE_MUSIC_NEXT_BEAT, "music drift: Beat");
static_assert(static_cast<int>(reverie::MusicTransition::NextBar) == REVERIE_MUSIC_NEXT_BAR, "music drift: Bar");
static_assert(static_cast<int>(reverie::FilterType::Lowpass) == REVERIE_FILTER_LOWPASS, "filter drift: LP");
static_assert(static_cast<int>(reverie::FilterType::Highpass) == REVERIE_FILTER_HIGHPASS, "filter drift: HP");
static_assert(static_cast<int>(reverie::FilterType::Bandpass) == REVERIE_FILTER_BANDPASS, "filter drift: BP");
static_assert(static_cast<int>(reverie::FilterType::Notch) == REVERIE_FILTER_NOTCH, "filter drift: Notch");
static_assert(static_cast<int>(reverie::FilterType::Peak) == REVERIE_FILTER_PEAK, "filter drift: Peak");
static_assert(static_cast<int>(reverie::FilterType::LowShelf) == REVERIE_FILTER_LOWSHELF, "filter drift: LS");
static_assert(static_cast<int>(reverie::FilterType::HighShelf) == REVERIE_FILTER_HIGHSHELF, "filter drift: HS");

// The C-ABI event builder is just a heap-allocated runtime EventDesc being assembled.
struct reverie_event_builder {
    reverie::EventDesc desc;
};

// Likewise the music-state builder assembles a MusicStateDesc.
struct reverie_music_builder {
    reverie::MusicStateDesc desc;
};

extern "C" {

unsigned int reverie_abi_version(void) { return REVERIE_ABI_VERSION; }

const char* reverie_result_string(reverie_result result) {
    switch (result) {
    case REVERIE_OK: return "ok";
    case REVERIE_ERROR: return "error";
    case REVERIE_INVALID_ARGUMENT: return "invalid argument";
    case REVERIE_NOT_INITIALIZED: return "not initialized";
    case REVERIE_ALREADY_INITIALIZED: return "already initialized";
    case REVERIE_DEVICE_ERROR: return "device error";
    case REVERIE_DECODE_ERROR: return "decode error";
    case REVERIE_FILE_NOT_FOUND: return "file not found";
    case REVERIE_UNSUPPORTED: return "unsupported";
    case REVERIE_OUT_OF_MEMORY: return "out of memory";
    case REVERIE_NOT_FOUND: return "not found";
    }
    return "unknown";
}

void reverie_default_config(reverie_config* out) {
    if (out == nullptr) return;
    out->struct_size = static_cast<unsigned int>(sizeof(reverie_config));
    out->backend = REVERIE_BACKEND_MINIAUDIO;
    out->sample_rate = 48000;
    out->channels = 2;
    out->period_frames = 0;
    out->max_voices = 64;
    out->use_resonance = 0;
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
    cfg.maxVoices = config->max_voices; // 0 -> default is applied once, in AudioEngine::Init
    cfg.useResonance = config->use_resonance != 0;
    return MapResult(Cast(engine)->Init(cfg));
}

void reverie_shutdown(reverie_engine* engine) {
    if (engine != nullptr) Cast(engine)->Shutdown();
}

int reverie_is_initialized(reverie_engine* engine) {
    return (engine != nullptr && Cast(engine)->IsInitialized()) ? 1 : 0;
}

reverie_result reverie_start(reverie_engine* engine) {
    if (engine == nullptr) return REVERIE_INVALID_ARGUMENT;
    return MapResult(Cast(engine)->Start());
}

reverie_result reverie_stop(reverie_engine* engine) {
    if (engine == nullptr) return REVERIE_INVALID_ARGUMENT;
    return MapResult(Cast(engine)->Stop());
}

void reverie_update(reverie_engine* engine) {
    if (engine != nullptr) Cast(engine)->Update();
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

reverie_sound reverie_load_sound_hdsrf(reverie_engine* engine, const unsigned char* data,
                                       size_t size) {
    return engine != nullptr ? Cast(engine)->LoadSoundHdsrf(data, size) : 0u;
}

reverie_result reverie_cook_hdsrf(const float* interleaved, unsigned int frame_count,
                                  unsigned int channels, unsigned int sample_rate,
                                  unsigned int chunk_frames, unsigned char* buf, size_t cap,
                                  size_t* needed) {
    std::vector<reverie::u8> bytes;
    const reverie_result r = MapResult(
        reverie::CookHdsrf(interleaved, frame_count, channels, sample_rate, chunk_frames, bytes));
    if (r != REVERIE_OK) return r;
    if (needed != nullptr) *needed = bytes.size();
    if (buf == nullptr) return REVERIE_OK;                     // size query
    if (cap < bytes.size()) return REVERIE_INVALID_ARGUMENT;   // too small (*needed set)
    if (!bytes.empty()) std::memcpy(buf, bytes.data(), bytes.size());
    return REVERIE_OK;
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

void reverie_get_stats(reverie_engine* engine, reverie_stats* out) {
    if (out == nullptr) return;
    *out = reverie_stats{};
    out->struct_size = static_cast<unsigned int>(sizeof(reverie_stats));
    if (engine == nullptr) return;
    const reverie::EngineStats s = Cast(engine)->GetStats();
    out->sample_rate = s.sampleRate;
    out->channels = s.channels;
    out->active_voices = s.activeVoices;
    out->real_voices = s.realVoices;
    out->virtual_voices = s.virtualVoices;
    out->cpu_load = s.cpuLoad;
    out->master_peak = s.masterPeak;
    out->music_beat = s.musicBeat;
    out->music_bar = s.musicBar;
    out->music_bpm = s.musicBpm;
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

reverie_result reverie_save_bank(reverie_engine* engine, unsigned char* buf, size_t cap,
                                 size_t* needed) {
    if (engine == nullptr) return REVERIE_INVALID_ARGUMENT;
    std::vector<reverie::u8> bytes;
    const reverie_result r = MapResult(Cast(engine)->SaveBank(bytes));
    if (r != REVERIE_OK) return r;
    if (needed != nullptr) *needed = bytes.size();
    if (buf == nullptr) return REVERIE_OK;            // size query
    if (cap < bytes.size()) return REVERIE_INVALID_ARGUMENT; // too small (*needed tells the caller)
    if (!bytes.empty()) std::memcpy(buf, bytes.data(), bytes.size());
    return REVERIE_OK;
}

reverie_result reverie_load_bank(reverie_engine* engine, const unsigned char* data, size_t size) {
    if (engine == nullptr) return REVERIE_INVALID_ARGUMENT;
    return MapResult(Cast(engine)->LoadBank(data, size));
}

reverie_music_builder* reverie_music_builder_create(const char* name, float bpm,
                                                    unsigned int beats_per_bar) {
    auto* b = new (std::nothrow) reverie_music_builder();
    if (b != nullptr) {
        b->desc.name = name != nullptr ? name : "";
        b->desc.bpm = bpm;
        b->desc.beatsPerBar = beats_per_bar;
    }
    return b;
}

int reverie_music_builder_add_layer(reverie_music_builder* builder, reverie_sound sound, float gain,
                                    reverie_parameter gain_param, float param_lo, float param_hi) {
    if (builder == nullptr) return -1;
    reverie::MusicLayerDesc l;
    l.sound = sound;
    l.gain = gain;
    l.gainParam = gain_param;
    l.paramLo = param_lo;
    l.paramHi = param_hi;
    builder->desc.layers.push_back(l);
    return static_cast<int>(builder->desc.layers.size()) - 1;
}

reverie_music_state reverie_music_builder_register(reverie_engine* engine,
                                                   reverie_music_builder* builder) {
    if (engine == nullptr || builder == nullptr) {
        delete builder;
        return 0u;
    }
    const reverie_music_state id = Cast(engine)->RegisterMusicState(builder->desc);
    delete builder;
    return id;
}

void reverie_music_builder_destroy(reverie_music_builder* builder) { delete builder; }

reverie_music_state reverie_find_music_state(reverie_engine* engine, const char* name) {
    return engine != nullptr ? Cast(engine)->FindMusicState(name) : 0u;
}

void reverie_set_music_state(reverie_engine* engine, reverie_music_state state) {
    if (engine != nullptr) Cast(engine)->SetMusicState(state);
}

void reverie_set_music_state_at(reverie_engine* engine, reverie_music_state state,
                                reverie_music_transition transition) {
    if (engine != nullptr)
        Cast(engine)->SetMusicState(state, static_cast<reverie::MusicTransition>(transition));
}

void reverie_stop_music(reverie_engine* engine) {
    if (engine != nullptr) Cast(engine)->StopMusic();
}

double reverie_music_beat(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->MusicBeat() : 0.0;
}

unsigned long long reverie_music_bar(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->MusicBar() : 0ull;
}

float reverie_music_bpm(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->MusicBpm() : 0.0f;
}

unsigned int reverie_load_sound_file_async(reverie_engine* engine, const char* path) {
    return engine != nullptr ? Cast(engine)->LoadSoundFileAsync(path) : 0u;
}

unsigned int reverie_load_sound_hdsrf_async(reverie_engine* engine, const unsigned char* data,
                                            size_t size) {
    return engine != nullptr ? Cast(engine)->LoadSoundHdsrfAsync(data, size) : 0u;
}

int reverie_poll_load(reverie_engine* engine, unsigned int request_id, reverie_sound* out_sound) {
    if (engine == nullptr) return 0;
    reverie::SoundId s = 0;
    if (!Cast(engine)->PollLoad(request_id, s)) return 0;
    if (out_sound != nullptr) *out_sound = s;
    return 1;
}

reverie_voice reverie_play_stream(reverie_engine* engine, const unsigned char* data, size_t size,
                                  float volume, int loop, reverie_bus bus) {
    return engine != nullptr ? Cast(engine)->PlayStream(data, size, volume, loop != 0, bus) : 0u;
}

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

void reverie_unregister_event(reverie_engine* engine, reverie_event event) {
    if (engine != nullptr) Cast(engine)->UnregisterEvent(event);
}

reverie_parameter reverie_register_parameter(reverie_engine* engine, const char* name,
                                             float default_value, float min_value, float max_value,
                                             float smooth_ms) {
    return engine != nullptr
               ? Cast(engine)->RegisterParameter(name, default_value, min_value, max_value, smooth_ms)
               : 0u;
}

reverie_parameter reverie_find_parameter(reverie_engine* engine, const char* name) {
    return engine != nullptr ? Cast(engine)->FindParameter(name) : 0u;
}

void reverie_set_parameter(reverie_engine* engine, reverie_parameter param, float value) {
    if (engine != nullptr) Cast(engine)->SetParameter(param, value);
}

float reverie_get_parameter(reverie_engine* engine, reverie_parameter param) {
    return engine != nullptr ? Cast(engine)->ParameterValue(param) : 0.0f;
}

float reverie_get_parameter_target(reverie_engine* engine, reverie_parameter param) {
    return engine != nullptr ? Cast(engine)->ParameterTarget(param) : 0.0f;
}

int reverie_bind_parameter_to_bus_gain(reverie_engine* engine, reverie_parameter param,
                                       reverie_bus bus) {
    return (engine != nullptr && Cast(engine)->BindParameterToBusGain(param, bus)) ? 1 : 0;
}

void reverie_event_builder_set_layer_bus(reverie_event_builder* builder, int layer,
                                         reverie_bus bus) {
    if (builder == nullptr || layer < 0) return;
    if (static_cast<std::size_t>(layer) >= builder->desc.layers.size()) return;
    builder->desc.layers[static_cast<std::size_t>(layer)].bus = bus;
}

void reverie_event_builder_set_layer_param(reverie_event_builder* builder, int layer,
                                           reverie_parameter param, float lo, float hi) {
    if (builder == nullptr || layer < 0) return;
    if (static_cast<std::size_t>(layer) >= builder->desc.layers.size()) return;
    auto& l = builder->desc.layers[static_cast<std::size_t>(layer)];
    l.gainParam = param;
    l.paramLo = lo;
    l.paramHi = hi;
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

int reverie_get_bus_muted(reverie_engine* engine, reverie_bus bus) {
    return (engine != nullptr && Cast(engine)->BusMuted(bus)) ? 1 : 0;
}

void reverie_set_bus_soloed(reverie_engine* engine, reverie_bus bus, int soloed) {
    if (engine != nullptr) Cast(engine)->SetBusSoloed(bus, soloed != 0);
}

int reverie_get_bus_soloed(reverie_engine* engine, reverie_bus bus) {
    return (engine != nullptr && Cast(engine)->BusSoloed(bus)) ? 1 : 0;
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

reverie_effect reverie_add_bus_effect(reverie_engine* engine, reverie_bus bus,
                                      reverie_effect_type type) {
    return engine != nullptr
               ? Cast(engine)->AddBusEffect(bus, static_cast<reverie::EffectType>(type))
               : 0u;
}

void reverie_set_effect_param(reverie_engine* engine, reverie_effect effect, unsigned int index,
                              float value) {
    if (engine != nullptr) Cast(engine)->SetEffectParam(effect, index, value);
}

float reverie_get_effect_param(reverie_engine* engine, reverie_effect effect, unsigned int index) {
    return engine != nullptr ? Cast(engine)->EffectParam(effect, index) : 0.0f;
}

void reverie_capture_snapshot(reverie_engine* engine, const char* name) {
    if (engine != nullptr) Cast(engine)->CaptureSnapshot(name);
}

int reverie_apply_snapshot(reverie_engine* engine, const char* name) {
    return (engine != nullptr && Cast(engine)->ApplySnapshot(name)) ? 1 : 0;
}

void reverie_set_listener(reverie_engine* engine, float px, float py, float pz, float fx, float fy,
                          float fz, float ux, float uy, float uz) {
    if (engine != nullptr)
        Cast(engine)->SetListener(reverie::Float3{px, py, pz}, reverie::Float3{fx, fy, fz},
                                  reverie::Float3{ux, uy, uz});
}

reverie_voice reverie_play_spatial(reverie_engine* engine, reverie_sound sound, float px, float py,
                                   float pz, float volume, int loop) {
    return engine != nullptr
               ? Cast(engine)->PlaySpatial(sound, reverie::Float3{px, py, pz}, volume, loop != 0)
               : 0u;
}

void reverie_set_voice_position(reverie_engine* engine, reverie_voice voice, float px, float py,
                                float pz) {
    if (engine != nullptr) Cast(engine)->SetVoicePosition(voice, reverie::Float3{px, py, pz});
}

reverie_event_instance reverie_play_event_at(reverie_engine* engine, reverie_event event, float px,
                                             float py, float pz, float volume) {
    return engine != nullptr
               ? Cast(engine)->PlayEventAt(event, reverie::Float3{px, py, pz}, volume)
               : 0u;
}

reverie_bus reverie_spatial_bus(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->SpatialBus() : 0u;
}

const char* reverie_spatial_backend_name(reverie_engine* engine) {
    return engine != nullptr ? Cast(engine)->SpatialBackendName() : "none";
}

} // extern "C"
