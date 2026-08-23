/* Reverie/SDK/CAPI/reverie.h - the flat C ABI.
 *
 * Copyright (c) Hollow Dream Studios. All rights reserved.
 *
 * The stable binding boundary. Plain C, opaque handles, no C++ or STL in the signatures, so
 * Unity (P/Invoke), Unreal, and any other language can bind Reverie without touching its
 * internals. The C++ facade in <Reverie/Reverie.h> is a convenience layered over the same
 * runtime; integrations should target THIS header.
 *
 * Result codes mirror reverie::Result exactly (0 = ok, negatives = errors).
 *
 * Threading contract:
 *   - Call the public API from a SINGLE control thread (your game/main thread). The functions are
 *     NOT safe to call concurrently from multiple threads on the same engine.
 *   - Audio is produced on an internal/OS audio thread (the device callback), or synchronously by
 *     reverie_render_offline on the Null backend. You never call that path directly and you must
 *     never block it.
 *   - reverie_create/reverie_destroy bracket an engine's lifetime; do not use a handle after
 *     destroy. (Full lock-free control<->audio hardening is in progress; see
 *     Docs/ArchitectureAssessment.md, Phase 1.)
 *
 * ABI stability: use reverie_abi_version() vs REVERIE_ABI_VERSION to detect a header/library
 * mismatch, and always initialize reverie_config via reverie_default_config (it sets struct_size).
 */
#ifndef REVERIE_CAPI_H
#define REVERIE_CAPI_H

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(REVERIE_BUILD_DLL)
#define REVERIE_API __declspec(dllexport)
#elif defined(REVERIE_DLL)
#define REVERIE_API __declspec(dllimport)
#else
#define REVERIE_API
#endif
#else
#define REVERIE_API
#endif

typedef struct reverie_engine reverie_engine;
typedef unsigned int reverie_sound;
typedef unsigned int reverie_voice;
typedef unsigned int reverie_event;
typedef unsigned int reverie_event_instance;
typedef unsigned int reverie_bus;
typedef unsigned int reverie_parameter;
typedef unsigned int reverie_effect;
typedef unsigned int reverie_music_state;
typedef struct reverie_event_builder reverie_event_builder;
typedef struct reverie_music_builder reverie_music_builder;

typedef enum reverie_effect_type {
    REVERIE_EFFECT_FILTER = 0,     /* biquad; params: 0=filter type, 1=cutoff Hz, 2=Q, 3=gain dB */
    REVERIE_EFFECT_COMPRESSOR = 1, /* params: 0=threshDb,1=ratio,2=attackMs,3=releaseMs,4=makeupDb */
    REVERIE_EFFECT_DELAY = 2       /* params: 0=delay ms, 1=feedback, 2=wet mix */
} reverie_effect_type;

typedef enum reverie_filter_type {
    REVERIE_FILTER_LOWPASS = 0,
    REVERIE_FILTER_HIGHPASS = 1,
    REVERIE_FILTER_BANDPASS = 2,
    REVERIE_FILTER_NOTCH = 3,
    REVERIE_FILTER_PEAK = 4,
    REVERIE_FILTER_LOWSHELF = 5,
    REVERIE_FILTER_HIGHSHELF = 6
} reverie_filter_type;

typedef enum reverie_backend {
    REVERIE_BACKEND_NULL = 0,
    REVERIE_BACKEND_MINIAUDIO = 1
} reverie_backend;

typedef enum reverie_result {
    REVERIE_OK = 0,
    REVERIE_ERROR = -1,
    REVERIE_INVALID_ARGUMENT = -2,
    REVERIE_NOT_INITIALIZED = -3,
    REVERIE_ALREADY_INITIALIZED = -4,
    REVERIE_DEVICE_ERROR = -5,
    REVERIE_DECODE_ERROR = -6,
    REVERIE_FILE_NOT_FOUND = -7,
    REVERIE_UNSUPPORTED = -8,
    REVERIE_OUT_OF_MEMORY = -9,
    REVERIE_NOT_FOUND = -10
} reverie_result;

/* ABI version of this header. A binding compares it against reverie_abi_version() (the value the
 * linked library was built with) to detect a header/library mismatch before using the API.
 * Bump this whenever the C ABI changes in a way that is not backward compatible. */
#define REVERIE_ABI_VERSION 1u

/* The ABI version the linked Reverie library was built with (compare to REVERIE_ABI_VERSION). */
REVERIE_API unsigned int reverie_abi_version(void);

/* A short human-readable, static (never-freed) string for a result code, e.g. for logging. */
REVERIE_API const char* reverie_result_string(reverie_result result);

typedef struct reverie_config {
    /* MUST be set to sizeof(reverie_config) by the caller (reverie_default_config does this). It
     * lets a newer library accept a struct from an older caller: future fields are appended and the
     * library reads only up to `struct_size`. Passing the struct BY POINTER (never by value) plus
     * this size field is what keeps the ABI growable without breaking already-compiled bindings. */
    unsigned int struct_size;
    reverie_backend backend;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int period_frames;
    unsigned int max_voices;  /* real-voice budget (0 -> default 64) */
    int use_resonance;        /* 1 -> HDS Resonance HRTF spatial backend if built, else panning */
} reverie_config;

/* Fills *out with defaults (struct_size, miniaudio, 48k, stereo). Always use this then override
 * the fields you care about, so struct_size is set correctly and new fields get sane defaults. */
REVERIE_API void reverie_default_config(reverie_config* out);

REVERIE_API reverie_engine* reverie_create(void);
REVERIE_API void reverie_destroy(reverie_engine* engine);

REVERIE_API reverie_result reverie_init(reverie_engine* engine, const reverie_config* config);
REVERIE_API void reverie_shutdown(reverie_engine* engine);
REVERIE_API int reverie_is_initialized(reverie_engine* engine); /* 1 if init'd, else 0 */
REVERIE_API reverie_result reverie_start(reverie_engine* engine);
REVERIE_API reverie_result reverie_stop(reverie_engine* engine);
/* Host per-frame control pump (applies quantized music transitions; more deferred work later). */
REVERIE_API void reverie_update(reverie_engine* engine);

typedef enum reverie_music_transition {
    REVERIE_MUSIC_IMMEDIATE = 0,
    REVERIE_MUSIC_NEXT_BEAT = 1,
    REVERIE_MUSIC_NEXT_BAR = 2
} reverie_music_transition;

REVERIE_API void reverie_set_master_volume(reverie_engine* engine, float volume);
REVERIE_API float reverie_get_master_volume(reverie_engine* engine);
REVERIE_API unsigned int reverie_output_channels(reverie_engine* engine);
REVERIE_API unsigned int reverie_output_sample_rate(reverie_engine* engine);

REVERIE_API reverie_sound reverie_load_sound_file(reverie_engine* engine, const char* path);
REVERIE_API reverie_sound reverie_load_sound_pcm(reverie_engine* engine, const float* interleaved,
                                                 unsigned int frame_count, unsigned int channels,
                                                 unsigned int sample_rate);
REVERIE_API void reverie_unload_sound(reverie_engine* engine, reverie_sound sound);

/* .HDSRF cooked audio: load a cooked blob into a sound, or cook interleaved f32 PCM into a blob.
 * cook: pass buf=NULL to query the size into *needed, then call again with a buffer of that size. */
REVERIE_API reverie_sound reverie_load_sound_hdsrf(reverie_engine* engine, const unsigned char* data,
                                                   size_t size);
REVERIE_API reverie_result reverie_cook_hdsrf(const float* interleaved, unsigned int frame_count,
                                              unsigned int channels, unsigned int sample_rate,
                                              unsigned int chunk_frames, unsigned char* buf,
                                              size_t cap, size_t* needed);

/* Asynchronous loading (background worker). Returns a request id; poll it for the SoundId. */
REVERIE_API unsigned int reverie_load_sound_file_async(reverie_engine* engine, const char* path);
REVERIE_API unsigned int reverie_load_sound_hdsrf_async(reverie_engine* engine,
                                                        const unsigned char* data, size_t size);
/* Returns 1 when the load has completed (and writes *out_sound; 0 = still pending / unknown id). */
REVERIE_API int reverie_poll_load(reverie_engine* engine, unsigned int request_id,
                                  reverie_sound* out_sound);

/* Streaming playback of an .HDSRF blob (decoded in the background, bounded memory). Returns a voice
 * id (0 if the stream pool is full). bus 0 = Master. */
REVERIE_API reverie_voice reverie_play_stream(reverie_engine* engine, const unsigned char* data,
                                              size_t size, float volume, int loop, reverie_bus bus);

REVERIE_API reverie_voice reverie_play(reverie_engine* engine, reverie_sound sound, float volume,
                                       int loop);
REVERIE_API void reverie_stop_voice(reverie_engine* engine, reverie_voice voice);
REVERIE_API void reverie_stop_all(reverie_engine* engine);
REVERIE_API unsigned int reverie_active_voice_count(reverie_engine* engine);

/* A profiling/debug snapshot. Set out->struct_size = sizeof(reverie_stats) before calling (or use
 * reverie_get_stats which fills it). */
typedef struct reverie_stats {
    unsigned int struct_size;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int active_voices;
    unsigned int real_voices;
    unsigned int virtual_voices;
    float cpu_load;   /* last block render time / block duration */
    float master_peak;
    double music_beat;
    unsigned long long music_bar;
    float music_bpm;
} reverie_stats;
REVERIE_API void reverie_get_stats(reverie_engine* engine, reverie_stats* out);

/* Offline pull of the whole graph (interleaved f32, reverie_output_channels frames). Returns
 * frames written. For the Null backend / tests / the offline renderer. */
REVERIE_API unsigned int reverie_render_offline(reverie_engine* engine, float* out,
                                                unsigned int frame_count);

/* -- Voice management ------------------------------------------------------------------- */
REVERIE_API void reverie_set_max_voices(reverie_engine* engine, unsigned int count);
REVERIE_API void reverie_set_seed(reverie_engine* engine, unsigned long long seed);
REVERIE_API unsigned int reverie_real_voice_count(reverie_engine* engine);
REVERIE_API unsigned int reverie_virtual_voice_count(reverie_engine* engine);

/* -- Events (layered) ------------------------------------------------------------------- *
 * Build an event: create a builder, add layers, add weighted sounds to each layer, then
 * register it (which frees the builder and returns an event id). */
REVERIE_API reverie_event_builder* reverie_event_builder_create(int priority,
                                                                unsigned int max_instances,
                                                                unsigned int concurrency_group);
/* Returns the new layer's index, or -1 on error. */
REVERIE_API int reverie_event_builder_add_layer(reverie_event_builder* builder, float volume,
                                                float volume_variance, float pitch,
                                                float pitch_variance, int loop, float probability);
REVERIE_API void reverie_event_builder_add_sound(reverie_event_builder* builder, int layer,
                                                 reverie_sound sound, float weight);
/* Registers the built event and destroys the builder. Returns the event id (0 on error). */
REVERIE_API reverie_event reverie_event_builder_register(reverie_engine* engine,
                                                         reverie_event_builder* builder);
/* Destroys a builder you decided not to register. */
REVERIE_API void reverie_event_builder_destroy(reverie_event_builder* builder);

/* -- Serialization (versioned bank: mixer bus tree + parameters) ------------------------ *
 * save: pass buf=NULL to query the size into *needed, then call again with a buffer of that size.
 * Returns REVERIE_INVALID_ARGUMENT if a non-NULL buffer is too small (*needed is still set).
 * load: validates + applies a bank; rejects a corrupt/newer buffer (REVERIE_UNSUPPORTED/ERROR). */
REVERIE_API reverie_result reverie_save_bank(reverie_engine* engine, unsigned char* buf, size_t cap,
                                             size_t* needed);
REVERIE_API reverie_result reverie_load_bank(reverie_engine* engine, const unsigned char* data,
                                             size_t size);

REVERIE_API reverie_event_instance reverie_play_event(reverie_engine* engine, reverie_event event,
                                                      float volume);
REVERIE_API void reverie_stop_event_instance(reverie_engine* engine,
                                             reverie_event_instance instance);
REVERIE_API unsigned int reverie_active_instance_count(reverie_engine* engine, reverie_event event);
/* Frees a registered event def (does not stop its live instances). */
REVERIE_API void reverie_unregister_event(reverie_engine* engine, reverie_event event);

/* Routes an event layer to a bus (call after add_layer, before register). */
REVERIE_API void reverie_event_builder_set_layer_bus(reverie_event_builder* builder, int layer,
                                                     reverie_bus bus);
/* Modulates an event layer's gain by a parameter (smoothstep over [lo,hi]); 0 = none. */
REVERIE_API void reverie_event_builder_set_layer_param(reverie_event_builder* builder, int layer,
                                                       reverie_parameter param, float lo, float hi);

/* -- Parameters (RTPC) ------------------------------------------------------------------ *
 * A named, ranged, smoothed float the game drives (e.g. "CombatIntensity"); the current value
 * eases toward the target over smooth_ms. Returns a stable id (or the existing id for that name;
 * 0 on failure). */
REVERIE_API reverie_parameter reverie_register_parameter(reverie_engine* engine, const char* name,
                                                         float default_value, float min_value,
                                                         float max_value, float smooth_ms);
REVERIE_API reverie_parameter reverie_find_parameter(reverie_engine* engine, const char* name);
REVERIE_API void reverie_set_parameter(reverie_engine* engine, reverie_parameter param, float value);
REVERIE_API float reverie_get_parameter(reverie_engine* engine, reverie_parameter param);
REVERIE_API float reverie_get_parameter_target(reverie_engine* engine, reverie_parameter param);
/* Drive a bus's gain from a parameter each block (mixer automation). Returns 1 on success. */
REVERIE_API int reverie_bind_parameter_to_bus_gain(reverie_engine* engine, reverie_parameter param,
                                                   reverie_bus bus);

/* -- Adaptive music --------------------------------------------------------------------- *
 * Build a music state (name/tempo + synchronized looping layers), register it, then switch to it.
 * A layer's gain can be driven by a parameter via smoothstep(param_lo, param_hi). */
REVERIE_API reverie_music_builder* reverie_music_builder_create(const char* name, float bpm,
                                                               unsigned int beats_per_bar);
/* Returns the new layer index, or -1 on error. gain_param 0 = no parameter modulation. */
REVERIE_API int reverie_music_builder_add_layer(reverie_music_builder* builder, reverie_sound sound,
                                                float gain, reverie_parameter gain_param,
                                                float param_lo, float param_hi);
/* Registers the state and destroys the builder. Returns the state id (0 on error). */
REVERIE_API reverie_music_state reverie_music_builder_register(reverie_engine* engine,
                                                              reverie_music_builder* builder);
REVERIE_API void reverie_music_builder_destroy(reverie_music_builder* builder);

REVERIE_API reverie_music_state reverie_find_music_state(reverie_engine* engine, const char* name);
REVERIE_API void reverie_set_music_state(reverie_engine* engine, reverie_music_state state);
/* Switch state at a beat/bar boundary (applied by reverie_update). */
REVERIE_API void reverie_set_music_state_at(reverie_engine* engine, reverie_music_state state,
                                            reverie_music_transition transition);
REVERIE_API void reverie_stop_music(reverie_engine* engine);
REVERIE_API double reverie_music_beat(reverie_engine* engine);
REVERIE_API unsigned long long reverie_music_bar(reverie_engine* engine);
REVERIE_API float reverie_music_bpm(reverie_engine* engine);

/* -- Mixer / bus tree ------------------------------------------------------------------- *
 * After reverie_init the default tree exists: Master + Music/SFX/Dialogue/Ambience/UI. */
REVERIE_API reverie_bus reverie_master_bus(reverie_engine* engine);
REVERIE_API reverie_bus reverie_create_bus(reverie_engine* engine, const char* name,
                                           reverie_bus parent);
REVERIE_API reverie_bus reverie_find_bus(reverie_engine* engine, const char* name);
REVERIE_API void reverie_set_bus_volume(reverie_engine* engine, reverie_bus bus, float volume);
REVERIE_API float reverie_get_bus_volume(reverie_engine* engine, reverie_bus bus);
REVERIE_API void reverie_set_bus_muted(reverie_engine* engine, reverie_bus bus, int muted);
REVERIE_API int reverie_get_bus_muted(reverie_engine* engine, reverie_bus bus);
REVERIE_API void reverie_set_bus_soloed(reverie_engine* engine, reverie_bus bus, int soloed);
REVERIE_API int reverie_get_bus_soloed(reverie_engine* engine, reverie_bus bus);
REVERIE_API void reverie_add_send(reverie_engine* engine, reverie_bus from, reverie_bus to,
                                  float level);
REVERIE_API void reverie_set_duck(reverie_engine* engine, reverie_bus ducked,
                                  reverie_bus sidechain, float threshold, float amount,
                                  float attack_ms, float release_ms);
REVERIE_API void reverie_clear_duck(reverie_engine* engine, reverie_bus ducked);
REVERIE_API float reverie_bus_meter(reverie_engine* engine, reverie_bus bus);

/* Per-bus DSP inserts. add_bus_effect returns an effect id (0 on failure); set/get_effect_param
 * drive it by index (Filter: 0=reverie_filter_type, 1=cutoff Hz, 2=Q, 3=gain dB). */
REVERIE_API reverie_effect reverie_add_bus_effect(reverie_engine* engine, reverie_bus bus,
                                                  reverie_effect_type type);
REVERIE_API void reverie_set_effect_param(reverie_engine* engine, reverie_effect effect,
                                          unsigned int index, float value);
REVERIE_API float reverie_get_effect_param(reverie_engine* engine, reverie_effect effect,
                                           unsigned int index);
REVERIE_API void reverie_capture_snapshot(reverie_engine* engine, const char* name);
REVERIE_API int reverie_apply_snapshot(reverie_engine* engine, const char* name);

/* -- Spatial (3D) ----------------------------------------------------------------------- *
 * A default panning spatializer is active after init. World space is right-handed; the
 * listener faces -Z by default. Positions are plain float triples. */
REVERIE_API void reverie_set_listener(reverie_engine* engine, float px, float py, float pz,
                                      float fx, float fy, float fz, float ux, float uy, float uz);
REVERIE_API reverie_voice reverie_play_spatial(reverie_engine* engine, reverie_sound sound,
                                               float px, float py, float pz, float volume,
                                               int loop);
REVERIE_API void reverie_set_voice_position(reverie_engine* engine, reverie_voice voice, float px,
                                            float py, float pz);
REVERIE_API reverie_event_instance reverie_play_event_at(reverie_engine* engine,
                                                         reverie_event event, float px, float py,
                                                         float pz, float volume);
REVERIE_API reverie_bus reverie_spatial_bus(reverie_engine* engine);
/* "Panning" or "Resonance" - the spatial backend actually in use. */
REVERIE_API const char* reverie_spatial_backend_name(reverie_engine* engine);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REVERIE_CAPI_H */
