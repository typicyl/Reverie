// Reverie/Runtime/Music/MusicClock.h - the musical timeline (tempo, bars, beats, quantization).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The heart of adaptive music: a sample-accurate musical clock advanced by the audio thread. It
// tracks elapsed musical time in BEATS (a double, so it never drifts), derives bars/beat-phase, and
// answers the question every quantized transition needs: "how long until the next bar/beat/grid
// boundary?" Pure and single-threaded by contract (owned + advanced by the audio thread); higher
// layers publish a snapshot for the control thread. Header-only: it is trivial arithmetic.
#pragma once

#include "Core/Types.h"

namespace reverie {

class MusicClock {
public:
    void Configure(f32 bpm, u32 beatsPerBar) {
        bpm_ = bpm > 1.0f ? bpm : 1.0f;
        beatsPerBar_ = beatsPerBar == 0 ? 4 : beatsPerBar;
    }
    void SetPlaying(bool playing) { playing_ = playing; }
    bool Playing() const { return playing_; }
    void Reset() { totalBeats_ = 0.0; }

    // Advance the clock by a block of `seconds` (audio thread). No-op when stopped.
    void Advance(f64 seconds) {
        if (playing_ && seconds > 0.0) totalBeats_ += seconds * (static_cast<f64>(bpm_) / 60.0);
    }

    f32 Bpm() const { return bpm_; }
    u32 BeatsPerBar() const { return beatsPerBar_; }
    f64 SecondsPerBeat() const { return 60.0 / static_cast<f64>(bpm_); }
    f64 TotalBeats() const { return totalBeats_; }
    u64 Bar() const { return static_cast<u64>(totalBeats_ / static_cast<f64>(beatsPerBar_)); }
    // Position within the current bar, in beats: [0, beatsPerBar).
    f64 PhaseInBar() const {
        const f64 bpb = static_cast<f64>(beatsPerBar_);
        f64 p = totalBeats_ - static_cast<f64>(Bar()) * bpb;
        if (p < 0.0) p = 0.0;
        return p;
    }

    // Beats remaining until the next boundary that is a multiple of `grid` beats. Result is in
    // (0, grid]: exactly on a boundary returns `grid` (the *next* one), so a quantized action never
    // fires "immediately" on a boundary it is already sitting on. grid<=0 returns 0 (no wait).
    f64 BeatsUntil(f64 grid) const {
        if (grid <= 0.0) return 0.0;
        const f64 rem = totalBeats_ - grid * static_cast<f64>(static_cast<i64>(totalBeats_ / grid));
        // rem in [0, grid); distance to next multiple:
        const f64 d = grid - rem;
        return d <= 0.0 ? grid : d;
    }

    // Convenience: beats/seconds until the next bar line.
    f64 BeatsUntilNextBar() const { return BeatsUntil(static_cast<f64>(beatsPerBar_)); }
    f64 SecondsUntilNextBar() const { return BeatsUntilNextBar() * SecondsPerBeat(); }

private:
    f32 bpm_ = 120.0f;
    u32 beatsPerBar_ = 4;
    f64 totalBeats_ = 0.0;
    bool playing_ = false;
};

} // namespace reverie
