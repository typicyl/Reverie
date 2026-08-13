// Reverie/Runtime/Spatial/ResonanceSpatialRenderer.h - HDS Resonance spatial backend factory.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The HDS Resonance Audio fork (github.com/hollowdstudios/HDS-resonance-audio) is an OPTIONAL
// backend: it is linked only when Reverie is built with -DREVERIE_WITH_RESONANCE=ON (and the
// fork + its deps are present). This factory returns a Resonance-backed ISpatialRenderer, or
// nullptr when the backend was not built - callers then fall back to the panning renderer.
// vraudio is confined entirely to ResonanceSpatialRenderer.cpp; no vraudio type appears here.
#pragma once

#include "Spatial/SpatialRenderer.h"

#include <memory>

namespace reverie {

// Returns a Resonance-backed renderer, or nullptr if REVERIE_WITH_RESONANCE was off at build time.
std::unique_ptr<ISpatialRenderer> CreateResonanceSpatialRenderer();

// True when the Resonance backend was compiled in.
bool ResonanceBackendBuilt();

} // namespace reverie
