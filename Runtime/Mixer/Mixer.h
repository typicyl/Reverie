// Reverie/Runtime/Mixer/Mixer.h - the routing/mixing bus tree.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A real mixer: Master plus arbitrary sub-buses (Music/SFX/Dialogue/Ambience/UI/...), each with
// gain, mute, solo, sends to other buses, an optional sidechain DUCK rule, and a peak METER.
// Voices accumulate into their target bus's block buffer; EndBlock then evaluates every bus in
// TOPOLOGICAL order (children, send sources, and sidechain sources first) applying gain / solo /
// mute / duck, metering, and routing each bus's signal into its parent and send destinations,
// finishing at Master -> the device output. SNAPSHOTS capture/restore named bus states.
//
// Single-threaded per block: BeginBlock / BusBuffer / EndBlock run on the audio thread; bus
// configuration (CreateBus/SetBus*/AddSend/SetDuck/snapshots) is expected from the game thread
// between blocks. (Fine for the offline test path; a command queue hardens this later.)
#pragma once

#include "Core/Types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace reverie {

class Mixer {
public:
    Mixer();

    // Builds the default tree: Master + Music, SFX, Dialogue, Ambience, UI (all under Master).
    void ConfigureDefault();

    BusId MasterBus() const { return master_; }
    BusId CreateBus(const char* name, BusId parent); // parent kInvalidId -> Master
    BusId FindBus(const char* name) const;           // kInvalidId if not found
    u32 BusCount() const { return static_cast<u32>(buses_.size()); }

    void SetBusVolume(BusId bus, f32 volume);
    f32 BusVolume(BusId bus) const;
    void SetBusMuted(BusId bus, bool muted);
    bool BusMuted(BusId bus) const;
    void SetBusSoloed(BusId bus, bool soloed);
    bool BusSoloed(BusId bus) const;

    // Routes a copy of `from`'s (gained) signal into `to` at `level`. Additive; multiple sends ok.
    void AddSend(BusId from, BusId to, f32 level);

    // Ducks `ducked` while `sidechain`'s level exceeds `threshold`, pulling its gain down by
    // `amount` (0..1) with attack/release smoothing. E.g. duck Music under Dialogue.
    void SetDuck(BusId ducked, BusId sidechain, f32 threshold, f32 amount, f32 attackMs,
                 f32 releaseMs);
    void ClearDuck(BusId ducked);

    f32 Meter(BusId bus) const; // last block's post-gain peak (0..)

    // Snapshots: named captures of every bus's volume + mute, re-applied later.
    void CaptureSnapshot(const char* name);
    bool ApplySnapshot(const char* name);

    // --- per-block audio-thread path ---
    void BeginBlock(u32 frameCount, u32 channels);   // clears every bus buffer
    f32* BusBuffer(BusId bus);                        // where voices accumulate (null if unknown)
    void EndBlock(f32* output, u32 frameCount, u32 channels, u32 sampleRate);

private:
    struct Send {
        BusId dest = kInvalidId;
        f32 level = 1.0f;
    };
    struct Bus {
        BusId id = kInvalidId;
        std::string name;
        BusId parent = kInvalidId;
        f32 gain = 1.0f;
        bool muted = false;
        bool soloed = false;
        std::vector<Send> sends;
        bool hasDuck = false;
        BusId duckSidechain = kInvalidId;
        f32 duckThreshold = 0.1f;
        f32 duckAmount = 0.5f;
        f32 duckAttackMs = 10.0f;
        f32 duckReleaseMs = 200.0f;
        f32 duckGain = 1.0f; // smoothed
        std::vector<f32> buffer;
        f32 meterPeak = 0.0f;
    };

    Bus* Get(BusId id);
    const Bus* Get(BusId id) const;
    void MarkTopologyDirty() { topoDirty_ = true; }
    void RebuildTopology();
    void ComputeSoloAudibility(std::vector<bool>& audible) const;

    std::vector<Bus> buses_;
    std::unordered_map<BusId, usize> index_;
    std::unordered_map<std::string, BusId> byName_;
    std::vector<usize> topoOrder_; // process order (Master last)
    bool topoDirty_ = true;
    BusId master_ = kInvalidId;
    BusId nextId_ = 1;
    u32 blockFrames_ = 0;
    u32 blockChannels_ = 0;

    struct SnapEntry {
        f32 volume;
        bool muted;
    };
    std::unordered_map<std::string, std::unordered_map<BusId, SnapEntry>> snapshots_;
};

} // namespace reverie
