// Reverie/Studio/App/Theme.h - centralized Reverie Studio visual language.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#pragma once

namespace reverie::studio {

// Applies Reverie's dark, professional ImGui style (colors, spacing, rounding). Called once at
// startup. Keeping styling in one place means the whole editor shares one visual identity.
void ApplyReverieTheme();

// Reverie accent color (RGBA 0..1), for custom-rendered widgets (meters, timeline, etc.).
struct ThemeColors {
    static void Accent(float out[4]);
    static void Meter(float out[4]);
    static void MeterHot(float out[4]);
    static void GridLine(float out[4]);
};

} // namespace reverie::studio
