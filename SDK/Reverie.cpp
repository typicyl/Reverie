// Reverie/SDK/Reverie.cpp - reverie::Engine facade over the runtime.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Reverie/Reverie.h"

#include "Audio/AudioEngine.h"

namespace reverie {

struct Engine::Impl {
    AudioEngine engine;
};

namespace {

AudioEventDef ToRuntime(const EventDesc& desc) {
    AudioEventDef def;
    def.priority = desc.priority;
    def.maxInstances = desc.maxInstances;
    def.concurrencyGroup = desc.concurrencyGroup;
    def.layers.reserve(desc.layers.size());
    for (const EventLayerDesc& l : desc.layers) {
        EventLayer layer;
        layer.volume = l.volume;
        layer.volumeVariance = l.volumeVariance;
        layer.pitch = l.pitch;
        layer.pitchVariance = l.pitchVariance;
        layer.loop = l.loop;
        layer.probability = l.probability;
        layer.bus = l.bus;
        layer.pool.reserve(l.pool.size());
        for (const EventPoolEntry& p : l.pool)
            layer.pool.push_back(EventSound{p.sound, p.weight});
        def.layers.push_back(std::move(layer));
    }
    return def;
}

} // namespace

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

Result Engine::Init(const Config& config) {
    EngineConfig ec;
    ec.backend = config.backend == Backend::Null ? DeviceBackend::Null : DeviceBackend::Miniaudio;
    ec.sampleRate = config.sampleRate;
    ec.channels = config.channels;
    ec.periodFrames = config.periodFrames;
    ec.maxVoices = config.maxVoices;
    return impl_->engine.Init(ec);
}

void Engine::Shutdown() { impl_->engine.Shutdown(); }
bool Engine::IsInitialized() const { return impl_->engine.IsInitialized(); }
Result Engine::Start() { return impl_->engine.Start(); }
Result Engine::Stop() { return impl_->engine.Stop(); }
void Engine::SetMasterVolume(f32 volume) { impl_->engine.SetMasterVolume(volume); }
f32 Engine::MasterVolume() const { return impl_->engine.MasterVolume(); }
u32 Engine::OutputChannels() const { return impl_->engine.OutputFormat().channels; }
u32 Engine::OutputSampleRate() const { return impl_->engine.OutputFormat().sampleRate; }
void Engine::SetMaxVoices(u32 count) { impl_->engine.SetMaxVoices(count); }
void Engine::SetSeed(u64 seed) { impl_->engine.SetSeed(seed); }

BusId Engine::MasterBus() const { return impl_->engine.MasterBus(); }
BusId Engine::CreateBus(const char* name, BusId parent) {
    return impl_->engine.CreateBus(name, parent);
}
BusId Engine::FindBus(const char* name) const { return impl_->engine.FindBus(name); }
void Engine::SetBusVolume(BusId bus, f32 volume) { impl_->engine.SetBusVolume(bus, volume); }
f32 Engine::BusVolume(BusId bus) const { return impl_->engine.BusVolume(bus); }
void Engine::SetBusMuted(BusId bus, bool muted) { impl_->engine.SetBusMuted(bus, muted); }
bool Engine::BusMuted(BusId bus) const { return impl_->engine.BusMuted(bus); }
void Engine::SetBusSoloed(BusId bus, bool soloed) { impl_->engine.SetBusSoloed(bus, soloed); }
bool Engine::BusSoloed(BusId bus) const { return impl_->engine.BusSoloed(bus); }
void Engine::AddSend(BusId from, BusId to, f32 level) { impl_->engine.AddSend(from, to, level); }
void Engine::SetDuck(BusId ducked, BusId sidechain, f32 threshold, f32 amount, f32 attackMs,
                     f32 releaseMs) {
    impl_->engine.SetDuck(ducked, sidechain, threshold, amount, attackMs, releaseMs);
}
void Engine::ClearDuck(BusId ducked) { impl_->engine.ClearDuck(ducked); }
f32 Engine::BusMeter(BusId bus) const { return impl_->engine.BusMeter(bus); }
void Engine::CaptureSnapshot(const char* name) { impl_->engine.CaptureSnapshot(name); }
bool Engine::ApplySnapshot(const char* name) { return impl_->engine.ApplySnapshot(name); }

SoundId Engine::LoadSoundFile(const char* path) { return impl_->engine.LoadFile(path); }
SoundId Engine::LoadSoundPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate) {
    return impl_->engine.LoadPCM(interleaved, frameCount, channels, sampleRate);
}
void Engine::UnloadSound(SoundId sound) { impl_->engine.UnloadSound(sound); }

VoiceId Engine::Play(SoundId sound, f32 volume, bool loop) {
    return impl_->engine.PlaySound(sound, volume, loop);
}
void Engine::StopVoice(VoiceId voice) { impl_->engine.StopVoice(voice); }
void Engine::StopAll() { impl_->engine.StopAll(); }

EventId Engine::RegisterEvent(const EventDesc& desc) {
    return impl_->engine.RegisterEvent(ToRuntime(desc));
}
void Engine::UnregisterEvent(EventId event) { impl_->engine.UnregisterEvent(event); }
InstanceId Engine::PlayEvent(EventId event, f32 volume) {
    return impl_->engine.PlayEvent(event, volume);
}
void Engine::StopEventInstance(InstanceId instance) { impl_->engine.StopInstance(instance); }
u32 Engine::ActiveInstanceCount(EventId event) const {
    return impl_->engine.ActiveInstanceCount(event);
}

u32 Engine::ActiveVoiceCount() const { return impl_->engine.ActiveVoiceCount(); }
u32 Engine::RealVoiceCount() const { return impl_->engine.RealVoiceCount(); }
u32 Engine::VirtualVoiceCount() const { return impl_->engine.VirtualVoiceCount(); }
u32 Engine::RenderOffline(f32* out, u32 frameCount) {
    return impl_->engine.RenderOffline(out, frameCount);
}

} // namespace reverie
