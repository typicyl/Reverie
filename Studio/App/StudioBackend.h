// Reverie/Studio/App/StudioBackend.h - the editor's platform + renderer abstraction.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// The editor talks ONLY to this interface for windowing/input/rendering; it never includes Win32,
// DX11, or the ImGui platform/renderer backend headers. That keeps Reverie Studio's editor code
// portable - a Linux (SDL/GL/Vulkan) or macOS (Metal) backend can be dropped in later by adding a
// new IStudioBackend implementation + a CreateStudioBackend() for that platform, with no change to
// the editor, panels, model, or runtime. The concrete backend owns the Dear ImGui context; the
// editor draws with plain ImGui:: calls between BeginFrame() and EndFrame().
#pragma once

#include <memory>

namespace reverie::studio {

struct BackendConfig {
    const char* title = "Reverie Studio";
    int width = 1600;
    int height = 900;
    bool enableDocking = true;
    bool enableViewports = true; // detach panels into OS windows where the platform supports it
};

class IStudioBackend {
public:
    virtual ~IStudioBackend() = default;

    // Pump OS events. Returns false when the user asked to close the window.
    virtual bool PumpEvents() = 0;
    // Begin a UI frame (platform + renderer NewFrame + ImGui::NewFrame). Draw ImGui after this.
    virtual void BeginFrame() = 0;
    // Finish the frame: render the ImGui draw data, update detached viewports, and present.
    virtual void EndFrame(const float clearColor[4]) = 0;

    virtual const char* Name() const = 0;
};

// Creates the platform-appropriate backend (Win32+DX11 today). Returns nullptr if no backend is
// available for this platform/build (e.g. a headless build), so the app can fail gracefully.
std::unique_ptr<IStudioBackend> CreateStudioBackend(const BackendConfig& config);

} // namespace reverie::studio
