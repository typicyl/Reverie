// Reverie/Studio/App/Panels.h - the editor's dockable panels (views over EditorApp).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Each panel is a pure view: it reads the model / preview engine off EditorApp and routes edits
// back through the command stack. No panel owns model or runtime state.
#pragma once

namespace reverie::studio {

class EditorApp;

void DrawAssetsPanel(EditorApp& app);
void DrawInspectorPanel(EditorApp& app);
void DrawMixerPanel(EditorApp& app);
void DrawConsolePanel(EditorApp& app);
void DrawEventsPanel(EditorApp& app);
void DrawMusicPanel(EditorApp& app);
void DrawProfilerPanel(EditorApp& app);
void DrawProjectSettingsPanel(EditorApp& app);

} // namespace reverie::studio
