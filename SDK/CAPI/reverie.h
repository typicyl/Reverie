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
 */
#ifndef REVERIE_CAPI_H
#define REVERIE_CAPI_H

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
typedef struct reverie_event_builder reverie_event_builder;

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

typedef struct reverie_config {
    reverie_backend backend;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int period_frames;
} reverie_config;

/* Returns a zeroed-then-defaulted config (miniaudio, 48k, stereo). */
REVERIE_API reverie_config reverie_default_config(void);

REVERIE_API reverie_engine* reverie_create(void);
REVERIE_API void reverie_destroy(reverie_engine* engine);

REVERIE_API reverie_result reverie_init(reverie_engine* engine, const reverie_config* config);
REVERIE_API void reverie_shutdown(reverie_engine* engine);
REVERIE_API reverie_result reverie_start(reverie_engine* engine);
REVERIE_API reverie_result reverie_stop(reverie_engine* engine);

REVERIE_API void reverie_set_master_volume(reverie_engine* engine, float volume);
REVERIE_API float reverie_get_master_volume(reverie_engine* engine);
REVERIE_API unsigned int reverie_output_channels(reverie_engine* engine);
REVERIE_API unsigned int reverie_output_sample_rate(reverie_engine* engine);

REVERIE_API reverie_sound reverie_load_sound_file(reverie_engine* engine, const char* path);
REVERIE_API reverie_sound reverie_load_sound_pcm(reverie_engine* engine, const float* interleaved,
                                                 unsigned int frame_count, unsigned int channels,
                                                 unsigned int sample_rate);
REVERIE_API void reverie_unload_sound(reverie_engine* engine, reverie_sound sound);

REVERIE_API reverie_voice reverie_play(reverie_engine* engine, reverie_sound sound, float volume,
                                       int loop);
REVERIE_API void reverie_stop_voice(reverie_engine* engine, reverie_voice voice);
REVERIE_API void reverie_stop_all(reverie_engine* engine);
REVERIE_API unsigned int reverie_active_voice_count(reverie_engine* engine);

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

REVERIE_API reverie_event_instance reverie_play_event(reverie_engine* engine, reverie_event event,
                                                      float volume);
REVERIE_API void reverie_stop_event_instance(reverie_engine* engine,
                                             reverie_event_instance instance);
REVERIE_API unsigned int reverie_active_instance_count(reverie_engine* engine, reverie_event event);

/* Routes an event layer to a bus (call after add_layer, before register). */
REVERIE_API void reverie_event_builder_set_layer_bus(reverie_event_builder* builder, int layer,
                                                     reverie_bus bus);

/* -- Mixer / bus tree ------------------------------------------------------------------- *
 * After reverie_init the default tree exists: Master + Music/SFX/Dialogue/Ambience/UI. */
REVERIE_API reverie_bus reverie_master_bus(reverie_engine* engine);
REVERIE_API reverie_bus reverie_create_bus(reverie_engine* engine, const char* name,
                                           reverie_bus parent);
REVERIE_API reverie_bus reverie_find_bus(reverie_engine* engine, const char* name);
REVERIE_API void reverie_set_bus_volume(reverie_engine* engine, reverie_bus bus, float volume);
REVERIE_API float reverie_get_bus_volume(reverie_engine* engine, reverie_bus bus);
REVERIE_API void reverie_set_bus_muted(reverie_engine* engine, reverie_bus bus, int muted);
REVERIE_API void reverie_set_bus_soloed(reverie_engine* engine, reverie_bus bus, int soloed);
REVERIE_API void reverie_add_send(reverie_engine* engine, reverie_bus from, reverie_bus to,
                                  float level);
REVERIE_API void reverie_set_duck(reverie_engine* engine, reverie_bus ducked,
                                  reverie_bus sidechain, float threshold, float amount,
                                  float attack_ms, float release_ms);
REVERIE_API void reverie_clear_duck(reverie_engine* engine, reverie_bus ducked);
REVERIE_API float reverie_bus_meter(reverie_engine* engine, reverie_bus bus);
REVERIE_API void reverie_capture_snapshot(reverie_engine* engine, const char* name);
REVERIE_API int reverie_apply_snapshot(reverie_engine* engine, const char* name);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REVERIE_CAPI_H */
