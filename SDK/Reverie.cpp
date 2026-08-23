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
        layer.gainParam = l.gainParam;
        layer.paramLo = l.paramLo;
        layer.paramHi = l.paramHi;
        layer.pool.reserve(l.pool.size());
        for (const EventPoolEntry& p : l.pool)
            layer.pool.push_back(EventSound{p.sound, p.weight});
        def.layers.push_back(std::move(layer));
    }
    return def;
}

MusicStateDef ToRuntime(const MusicStateDesc& desc) {
    MusicStateDef def;
    def.name = desc.name;
    def.bpm = desc.bpm;
    def.beatsPerBar = desc.beatsPerBar;
    def.layers.reserve(desc.layers.size());
    for (const MusicLayerDesc& l : desc.layers)
        def.layers.push_back(MusicLayerDef{l.sound, l.gain, l.gainParam, l.paramLo, l.paramHi});
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
    ec.useResonance = config.useResonance;
    return impl_->engine.Init(ec);
}

void Engine::Shutdown() { impl_->engine.Shutdown(); }
bool Engine::IsInitialized() const { return impl_->engine.IsInitialized(); }
Result Engine::Start() { return impl_->engine.Start(); }
Result Engine::Stop() { return impl_->engine.Stop(); }
void Engine::Update() { impl_->engine.Update(); }
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
EffectId Engine::AddBusEffect(BusId bus, EffectType type) {
    return impl_->engine.AddBusEffect(bus, type);
}
void Engine::SetEffectParam(EffectId effect, u32 index, f32 value) {
    impl_->engine.SetEffectParam(effect, index, value);
}
f32 Engine::EffectParam(EffectId effect, u32 index) const {
    return impl_->engine.EffectParam(effect, index);
}
void Engine::CaptureSnapshot(const char* name) { impl_->engine.CaptureSnapshot(name); }
bool Engine::ApplySnapshot(const char* name) { return impl_->engine.ApplySnapshot(name); }

void Engine::SetListener(const Float3& position, const Float3& forward, const Float3& up) {
    impl_->engine.SetListener(position, forward, up);
}
VoiceId Engine::PlaySpatial(SoundId sound, const Float3& position, f32 volume, bool loop) {
    return impl_->engine.PlaySpatial(sound, position, volume, loop);
}
void Engine::SetVoicePosition(VoiceId voice, const Float3& position) {
    impl_->engine.SetVoicePosition(voice, position);
}
InstanceId Engine::PlayEventAt(EventId event, const Float3& position, f32 volume) {
    return impl_->engine.PlayEventAt(event, position, volume);
}
BusId Engine::SpatialBus() const { return impl_->engine.SpatialBus(); }
const char* Engine::SpatialBackendName() const { return impl_->engine.SpatialBackendName(); }

SoundId Engine::LoadSoundFile(const char* path) { return impl_->engine.LoadFile(path); }
SoundId Engine::LoadSoundPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate) {
    return impl_->engine.LoadPCM(interleaved, frameCount, channels, sampleRate);
}
SoundId Engine::LoadSoundHdsrf(const u8* data, usize size) {
    return impl_->engine.LoadHdsrf(data, size);
}
u32 Engine::LoadSoundFileAsync(const char* path) { return impl_->engine.LoadFileAsync(path); }
u32 Engine::LoadSoundHdsrfAsync(const u8* data, usize size) {
    return impl_->engine.LoadHdsrfAsync(data, size);
}
bool Engine::PollLoad(u32 requestId, SoundId& outSound) {
    return impl_->engine.PollLoad(requestId, outSound);
}
VoiceId Engine::PlayStream(const u8* hdsrf, usize size, f32 volume, bool loop, BusId bus) {
    return impl_->engine.PlayStream(hdsrf, size, volume, loop, bus);
}
void Engine::UnloadSound(SoundId sound) { impl_->engine.UnloadSound(sound); }

VoiceId Engine::Play(SoundId sound, f32 volume, bool loop) {
    return impl_->engine.PlaySound(sound, volume, loop);
}
void Engine::StopVoice(VoiceId voice) { impl_->engine.StopVoice(voice); }
void Engine::StopAll() { impl_->engine.StopAll(); }

ParameterId Engine::RegisterParameter(const char* name, f32 defaultValue, f32 minValue,
                                      f32 maxValue, f32 smoothMs) {
    return impl_->engine.RegisterParameter(name, defaultValue, minValue, maxValue, smoothMs);
}
ParameterId Engine::FindParameter(const char* name) const {
    return impl_->engine.FindParameter(name);
}
void Engine::SetParameter(ParameterId param, f32 value) { impl_->engine.SetParameter(param, value); }
f32 Engine::ParameterValue(ParameterId param) const { return impl_->engine.ParameterValue(param); }
f32 Engine::ParameterTarget(ParameterId param) const { return impl_->engine.ParameterTarget(param); }
bool Engine::BindParameterToBusGain(ParameterId param, BusId bus) {
    return impl_->engine.BindParameterToBusGain(param, bus);
}

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

MusicStateId Engine::RegisterMusicState(const MusicStateDesc& desc) {
    return impl_->engine.RegisterMusicState(ToRuntime(desc));
}
MusicStateId Engine::FindMusicState(const char* name) const {
    return impl_->engine.FindMusicState(name);
}
void Engine::SetMusicState(MusicStateId state, MusicTransition transition) {
    impl_->engine.SetMusicState(state, transition);
}
void Engine::StopMusic() { impl_->engine.StopMusic(); }
MusicStateId Engine::CurrentMusicState() const { return impl_->engine.CurrentMusicState(); }
f64 Engine::MusicBeat() const { return impl_->engine.MusicBeat(); }
u64 Engine::MusicBar() const { return impl_->engine.MusicBar(); }
f32 Engine::MusicBpm() const { return impl_->engine.MusicBpm(); }

Result Engine::SaveBank(std::vector<u8>& out) const { return impl_->engine.SaveBank(out); }
Result Engine::LoadBank(const u8* data, usize size) { return impl_->engine.LoadBank(data, size); }

u32 Engine::ActiveVoiceCount() const { return impl_->engine.ActiveVoiceCount(); }
u32 Engine::RealVoiceCount() const { return impl_->engine.RealVoiceCount(); }
u32 Engine::VirtualVoiceCount() const { return impl_->engine.VirtualVoiceCount(); }
EngineStats Engine::GetStats() const { return impl_->engine.GetStats(); }
u32 Engine::RenderOffline(f32* out, u32 frameCount) {
    return impl_->engine.RenderOffline(out, frameCount);
}

} // namespace reverie
