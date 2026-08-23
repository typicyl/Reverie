// Reverie/Studio/App/gui_main.cpp - entry point for the Reverie Studio GUI (reverie-studio-gui).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Deliberately trivial: all application logic lives in EditorApp. This file just constructs it and
// runs the frame loop, so the entry point carries no platform, ImGui, or model knowledge.
#include "EditorApp.h"

int main() {
    reverie::studio::EditorApp app;
    return app.Run();
}
