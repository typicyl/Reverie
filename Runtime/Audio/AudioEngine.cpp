// Reverie/Runtime/Audio/AudioEngine.cpp - see AudioEngine.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Audio/AudioEngine.h"

#include "Audio/AudioDecoder.h"
#include "Core/Log.h"
#include "Serialization/Bank.h"
#include "Serialization/Hdsrf.h"
#include "Spatial/ResonanceSpatialRenderer.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace reverie {

AudioEngine::AudioEngine()
    : events_(voices_, [this](SoundId s) { return GetSound(s); }),
      music_(voices_, [this](SoundId s) { return GetSound(s); }) {}

AudioEngine::~AudioEngine() { Shutdown(); }

Result AudioEngine::Init(const EngineConfig& config) {
    if (inited_) return Result::AlreadyInitialized;
    if (config.channels == 0 || config.sampleRate == 0) return Result::InvalidArgument;

    DeviceDesc desc;
    desc.backend = config.backend;
    desc.format = AudioFormat{config.channels, config.sampleRate, SampleFormat::F32};
    desc.periodFrames = config.periodFrames;

    device_ = deviceManager_.CreateDevice(desc, this);
    if (!device_) {
        LogMessage(LogLevel::Error, "AudioEngine::Init: failed to create output device");
        return Result::DeviceError;
    }
    format_ = device_->Format(); // device is authoritative
    // 0 means "use the default budget" - applied here so the C ABI and the C++ facade agree (a
    // caller passing 0 gets 64 either way, instead of 64 via C and 1 via C++).
    const u32 effectiveMaxVoices = config.maxVoices != 0 ? config.maxVoices : 64;
    // Fixed voice pool: total voices (real + virtualized) are bounded, sized generously above the
    // real-voice budget so over-budget voices still get a slot to virtualize in.
    voices_.Reserve(std::max<u32>(256u, effectiveMaxVoices * 4u));
    voices_.SetMaxRealVoices(effectiveMaxVoices);
    params_.Reserve(256); // game parameters (RTPC) - the substrate for music/DSP automation
    voices_.SetParameterStore(&params_); // enable per-voice volume modulation by parameters
    streams_.Start(/*slots*/ 16, /*ringFramesPerSlot*/ 32768); // ~0.68s ring @48k stereo
    voices_.SetStreamManager(&streams_);
    mixer_.ConfigureDefault(); // Master + Music/SFX/Dialogue/Ambience/UI/Spatial
    music_.SetBus(mixer_.FindBus("Music"));
    // Spatial backend: HDS Resonance (HRTF) when requested and built into this binary, else the
    // dependency-free panning renderer. Both implement ISpatialRenderer.
    if (config.useResonance) {
        spatialRenderer_ = CreateResonanceSpatialRenderer();
        if (spatialRenderer_ && Failed(spatialRenderer_->Init(format_.sampleRate, 128)))
            spatialRenderer_.reset();
        if (!spatialRenderer_)
            LogMessage(LogLevel::Warning,
                       "AudioEngine: Resonance spatial backend unavailable; using panning.");
    }
    if (!spatialRenderer_) {
        spatialRenderer_ = std::make_unique<PanningSpatialRenderer>();
        spatialRenderer_->Init(format_.sampleRate, 128);
    }
    voices_.SetSpatialRenderer(spatialRenderer_.get());
    spatialBus_ = mixer_.FindBus("Spatial");
    StartLoader();
    inited_ = true;
    LogFormat(LogLevel::Info, "AudioEngine ready: %s, %u ch, %u Hz, %u voice budget",
              device_->Name(), format_.channels, format_.sampleRate, effectiveMaxVoices);
    return Result::Ok;
}

void AudioEngine::Shutdown() {
    if (!inited_) return;
    StopLoader(); // join the async loader before tearing down the sound table it writes into
    if (device_) {
        device_->Stop();
        device_.reset();
    }
    music_.Stop();
    voices_.ReleaseAllForShutdown(); // device already stopped: release spatial/stream slots + drop buffers
    streams_.Stop();                 // join the decode worker + free stream slots
    events_.StopAllInstances();
    voices_.SetSpatialRenderer(nullptr);
    spatialRenderer_.reset();
    spatialBus_ = kInvalidId;
    {
        std::lock_guard<std::mutex> lock(soundMutex_);
        sounds_.clear();
    }
    inited_ = false;
}

Result AudioEngine::Start() {
    if (!inited_ || !device_) return Result::NotInitialized;
    return device_->Start();
}

Result AudioEngine::Stop() {
    if (!inited_ || !device_) return Result::NotInitialized;
    return device_->Stop();
}

SoundId AudioEngine::RegisterSound(std::shared_ptr<const AudioBuffer> buffer) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    const SoundId id = nextSound_++;
    if (nextSound_ == kInvalidId) nextSound_ = 1;
    sounds_[id] = std::move(buffer);
    return id;
}

SoundId AudioEngine::LoadPCM(const f32* interleaved, u32 frameCount, u32 channels, u32 sampleRate) {
    if (interleaved == nullptr || frameCount == 0 || channels == 0 || sampleRate == 0)
        return kInvalidId;
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->channels = channels;
    buffer->sampleRate = sampleRate;
    buffer->samples.assign(interleaved,
                           interleaved + static_cast<usize>(frameCount) * channels);
    return RegisterSound(std::move(buffer));
}

SoundId AudioEngine::LoadFile(const char* path) {
    auto buffer = std::make_shared<AudioBuffer>();
    if (Failed(AudioDecoder::DecodeFile(path, *buffer))) return kInvalidId;
    return RegisterSound(std::move(buffer));
}

void AudioEngine::StartLoader() {
    if (loaderStarted_) return;
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        loaderQuit_ = false;
    }
    loaderThread_ = std::thread([this] { LoaderMain(); });
    loaderStarted_ = true;
}

void AudioEngine::StopLoader() {
    if (!loaderStarted_) return;
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        loaderQuit_ = true;
    }
    loadCv_.notify_all();
    if (loaderThread_.joinable()) loaderThread_.join();
    loaderStarted_ = false;
    // Drop any unprocessed requests/results so a later Init starts clean.
    std::lock_guard<std::mutex> lock(loadMutex_);
    std::queue<LoadRequest> empty;
    loadQueue_.swap(empty);
    loadDone_.clear();
}

void AudioEngine::LoaderMain() {
    for (;;) {
        LoadRequest req;
        {
            std::unique_lock<std::mutex> lock(loadMutex_);
            loadCv_.wait(lock, [this] { return loaderQuit_ || !loadQueue_.empty(); });
            if (loaderQuit_) return;              // abandon pending requests on shutdown
            req = std::move(loadQueue_.front());
            loadQueue_.pop();
        }
        // Decode OUTSIDE the lock (this is the whole point - off the control thread).
        auto buffer = std::make_shared<AudioBuffer>();
        Result r;
        if (req.isHdsrf)
            r = ReadHdsrf(req.blob.data(), req.blob.size(), *buffer);
        else
            r = AudioDecoder::DecodeFile(req.path.c_str(), *buffer);
        const SoundId sound = Failed(r) ? kInvalidId : RegisterSound(std::move(buffer));
        {
            std::lock_guard<std::mutex> lock(loadMutex_);
            loadDone_[req.id] = sound;
        }
    }
}

u32 AudioEngine::LoadFileAsync(const char* path) {
    if (path == nullptr) return 0;
    const u32 id = nextLoadId_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        LoadRequest req;
        req.id = id;
        req.isHdsrf = false;
        req.path = path;
        loadQueue_.push(std::move(req));
    }
    loadCv_.notify_one();
    return id;
}

u32 AudioEngine::LoadHdsrfAsync(const u8* data, usize size) {
    if (data == nullptr || size == 0) return 0;
    const u32 id = nextLoadId_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        LoadRequest req;
        req.id = id;
        req.isHdsrf = true;
        req.blob.assign(data, data + size);
        loadQueue_.push(std::move(req));
    }
    loadCv_.notify_one();
    return id;
}

VoiceId AudioEngine::PlayStream(const u8* hdsrf, usize size, f32 volume, bool loop, BusId bus) {
    const int slot = streams_.Open(hdsrf, size, loop);
    if (slot < 0) return kInvalidId;
    VoiceSpawn spawn;
    spawn.streamSlot = slot;
    spawn.volume = volume;
    spawn.loop = loop;
    spawn.bus = bus;
    spawn.priority = 500; // streams (usually music/ambience) outrank casual SFX
    const VoiceId id = voices_.Play(spawn);
    if (id == kInvalidId) streams_.RequestRelease(slot); // pool full: give the stream slot back
    return id;
}

bool AudioEngine::PollLoad(u32 requestId, SoundId& outSound) {
    std::lock_guard<std::mutex> lock(loadMutex_);
    auto it = loadDone_.find(requestId);
    if (it == loadDone_.end()) return false;
    outSound = it->second;
    loadDone_.erase(it);
    return true;
}

bool AudioEngine::BindParameterToBusGain(ParameterId param, BusId bus) {
    if (param == kInvalidId || bus == kInvalidId) return false;
    const u32 n = gainBindingCount_.load(std::memory_order_relaxed);
    if (n >= gainBindings_.size()) return false;
    gainBindings_[n] = GainBinding{param, bus};      // fill the slot...
    gainBindingCount_.store(n + 1, std::memory_order_release); // ...then publish it
    return true;
}

SoundId AudioEngine::LoadHdsrf(const u8* data, usize size) {
    auto buffer = std::make_shared<AudioBuffer>();
    if (Failed(ReadHdsrf(data, size, *buffer))) return kInvalidId;
    return RegisterSound(std::move(buffer));
}

void AudioEngine::UnloadSound(SoundId sound) {
    std::lock_guard<std::mutex> lock(soundMutex_);
    sounds_.erase(sound);
}

std::shared_ptr<const AudioBuffer> AudioEngine::GetSound(SoundId sound) const {
    std::lock_guard<std::mutex> lock(soundMutex_);
    auto it = sounds_.find(sound);
    return it != sounds_.end() ? it->second : nullptr;
}

VoiceId AudioEngine::PlaySound(SoundId sound, f32 volume, bool loop) {
    std::shared_ptr<const AudioBuffer> buffer = GetSound(sound);
    if (!buffer) return kInvalidId;
    VoiceSpawn spawn;
    spawn.buffer = std::move(buffer);
    spawn.volume = volume;
    spawn.loop = loop;
    return voices_.Play(spawn);
}

void AudioEngine::StopAll() {
    events_.StopAllInstances();
    voices_.StopAll();
}

void AudioEngine::RenderAudio(f32* output, u32 frameCount, u32 channels, u32 /*sampleRate*/) {
    if (output == nullptr || frameCount == 0 || channels == 0) return;
    const auto renderStart = std::chrono::steady_clock::now();
    const f64 blockSeconds = static_cast<f64>(frameCount) / static_cast<f64>(format_.sampleRate);
    // Advance game parameters (RTPC) toward their targets, then the musical clock, before the mix
    // (voices read parameter values for volume modulation this block).
    params_.Advance(static_cast<f32>(blockSeconds));
    music_.Update(blockSeconds);
    // Apply parameter->bus-gain automation (same thread as EndBlock, so writing bus gain is safe).
    const u32 nBindings = gainBindingCount_.load(std::memory_order_acquire);
    for (u32 i = 0; i < nBindings; ++i)
        mixer_.SetBusVolume(gainBindings_[i].bus, params_.Value(gainBindings_[i].param));
    // voices -> per-bus buffers (+ spatial voices -> renderer) -> bus tree -> Master -> output.
    mixer_.BeginBlock(frameCount, channels);
    if (spatialRenderer_) {
        spatialRenderer_->BeginBlock(frameCount);
        spatialRenderer_->SetListener(listenerPos_, listenerFwd_, listenerUp_);
        // Apply a pending environment on the audio thread (never concurrently with Render).
        if (envDirty_.exchange(false, std::memory_order_acquire))
            spatialRenderer_->SetEnvironment(pendingEnv_);
    }
    voices_.MixToBuses(mixer_, frameCount, channels, format_.sampleRate);
    if (spatialRenderer_ && spatialBus_ != kInvalidId) {
        const usize need = static_cast<usize>(frameCount) * 2;
        if (spatialTmp_.size() < need) spatialTmp_.assign(need, 0.0f);
        spatialRenderer_->Render(spatialTmp_.data(), frameCount);
        if (f32* sbus = mixer_.BusBuffer(spatialBus_)) {
            for (u32 f = 0; f < frameCount; ++f) {
                sbus[static_cast<usize>(f) * channels + 0] +=
                    spatialTmp_[static_cast<usize>(f) * 2 + 0];
                if (channels >= 2)
                    sbus[static_cast<usize>(f) * channels + 1] +=
                        spatialTmp_[static_cast<usize>(f) * 2 + 1];
            }
        }
    }
    mixer_.EndBlock(output, frameCount, channels, format_.sampleRate);

    // CPU load: fraction of the block's real-time budget this render took.
    const f64 elapsed = std::chrono::duration<f64>(std::chrono::steady_clock::now() - renderStart).count();
    cpuLoad_.store(blockSeconds > 0.0 ? static_cast<f32>(elapsed / blockSeconds) : 0.0f,
                   std::memory_order_relaxed);
}

EngineStats AudioEngine::GetStats() const {
    EngineStats s;
    s.sampleRate = format_.sampleRate;
    s.channels = format_.channels;
    s.activeVoices = voices_.ActiveVoiceCount();
    s.realVoices = voices_.RealVoiceCount();
    s.virtualVoices = voices_.VirtualVoiceCount();
    s.cpuLoad = cpuLoad_.load(std::memory_order_relaxed);
    s.masterPeak = mixer_.Meter(mixer_.MasterBus());
    s.musicBeat = music_.CurrentBeat();
    s.musicBar = music_.CurrentBar();
    s.musicBpm = music_.Bpm();
    return s;
}

void AudioEngine::SetListener(const Float3& position, const Float3& forward, const Float3& up) {
    listenerPos_ = position;
    listenerFwd_ = forward;
    listenerUp_ = up;
}

VoiceId AudioEngine::PlaySpatial(SoundId sound, const Float3& position, f32 volume, bool loop) {
    std::shared_ptr<const AudioBuffer> buffer = GetSound(sound);
    if (!buffer) return kInvalidId;
    VoiceSpawn spawn;
    spawn.buffer = std::move(buffer);
    spawn.volume = volume;
    spawn.loop = loop;
    spawn.spatial = true;
    spawn.position = position;
    spawn.bus = spatialBus_; // fallback bus if the source pool is full
    return voices_.Play(spawn);
}

Result AudioEngine::SaveBank(std::vector<u8>& out) const {
    BankData bd;
    bd.version = kBankVersion;

    ParameterStore::Descriptor pd;
    for (u32 i = 0; i < params_.Count(); ++i) {
        if (!params_.DescribeAt(i, pd)) continue;
        bd.parameters.push_back(
            BankParam{pd.name, pd.defaultValue, pd.minValue, pd.maxValue, pd.smoothMs});
    }

    Mixer::BusDescriptor bdesc;
    for (u32 i = 0; i < mixer_.BusCount(); ++i) {
        if (!mixer_.DescribeBusAt(i, bdesc)) continue;
        BankBus bb;
        bb.name = bdesc.name;
        bb.parentName = bdesc.parentName;
        bb.gain = bdesc.gain;
        bb.muted = bdesc.muted;
        bb.soloed = bdesc.soloed;
        for (const auto& s : bdesc.sends) bb.sends.push_back(BankSend{s.first, s.second});
        bd.buses.push_back(std::move(bb));
    }
    return WriteBank(bd, out);
}

Result AudioEngine::LoadBank(const u8* data, usize size) {
    BankData bd;
    const Result r = ReadBank(data, size, bd);
    if (Failed(r)) return r;

    for (const BankParam& p : bd.parameters)
        params_.Register(p.name.c_str(), p.defaultValue, p.minValue, p.maxValue, p.smoothMs);

    // Pass 1: create/update buses (creation order => parents precede children).
    for (const BankBus& b : bd.buses) {
        const BusId parent = b.parentName.empty() ? mixer_.MasterBus() : mixer_.FindBus(b.parentName.c_str());
        const BusId id = mixer_.CreateBus(b.name.c_str(), parent);
        if (id == kInvalidId) continue; // Master resolves via CreateBus("Master") -> existing
        mixer_.SetBusVolume(id, b.gain);
        mixer_.SetBusMuted(id, b.muted);
        mixer_.SetBusSoloed(id, b.soloed);
    }
    // Pass 2: wire sends now that every bus exists.
    for (const BankBus& b : bd.buses) {
        const BusId from = mixer_.FindBus(b.name.c_str());
        if (from == kInvalidId) continue;
        for (const BankSend& s : b.sends) {
            const BusId to = mixer_.FindBus(s.destName.c_str());
            if (to != kInvalidId) mixer_.AddSend(from, to, s.level);
        }
    }
    return Result::Ok;
}

u32 AudioEngine::RenderOffline(f32* out, u32 frameCount) {
    if (!inited_ || out == nullptr || frameCount == 0) return 0;
    RenderAudio(out, frameCount, format_.channels, format_.sampleRate);
    return frameCount;
}

} // namespace reverie
