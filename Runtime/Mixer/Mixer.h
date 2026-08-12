// Reverie/Runtime/Mixer/Mixer.h - the mixing bus tree.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Phase 1 is intentionally a single Master bus (gain + mute). The full tree - sub-buses,
// sends/returns, snapshots, metering, ducking/sidechain, channel limits - lands in the mixer
// phase; this type is the seam it grows into, so the engine already routes through a Mixer
// rather than a bare float.
#pragma once

#include "Core/Types.h"

namespace reverie {

struct BusState {
    f32 volume = 1.0f;
    bool muted = false;
    f32 Gain() const { return muted ? 0.0f : volume; }
};

class Mixer {
public:
    f32 MasterGain() const { return master_.Gain(); }
    f32 MasterVolume() const { return master_.volume; }
    bool MasterMuted() const { return master_.muted; }
    void SetMasterVolume(f32 v) { master_.volume = v < 0.0f ? 0.0f : v; }
    void SetMasterMuted(bool m) { master_.muted = m; }

private:
    BusState master_;
};

} // namespace reverie
