// Reverie/Studio/App/EditorApp.h - the Reverie Studio editor application.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Owns the authoring model (StudioApp/StudioProject), a live preview Engine, the editor state
// (selection, dirty flag, panel visibility), the undo/redo command stack, and the console log. Run()
// drives the frame loop through the abstract IStudioBackend and draws the ImGui UI. All model
// mutations go through the command stack; panels never touch the runtime directly except to preview.
#pragma once

#include "EditorCommand.h"
#include "Reverie/Reverie.h"
#include "StudioApp.h"
#include "StudioProject.h"

#include <string>
#include <vector>

namespace reverie::studio {

enum class SelKind { None, Asset, Bus, Param, Event, Music };

struct LogLine {
    std::string category;
    std::string message;
    int severity = 0; // 0 info, 1 warning, 2 error
};

struct EditorState {
    SelKind sel = SelKind::None;
    int selIndex = -1;
    bool dirty = false;
    std::string projectPath;
    bool built = false;         // BuildPreview has run against the current model
    bool audioAvailable = false; // preview engine opened a real audio device

    // Panel visibility (persisted by ImGui .ini via window open/close where relevant).
    bool pAssets = true, pInspector = true, pMixer = true, pConsole = true;
    bool pEvents = true, pMusic = true, pProfiler = true, pProjectSettings = false, pDemo = false;
};

class EditorApp {
public:
    EditorApp() = default;
    int Run(); // create backend + preview engine, run the loop; returns an exit code

    // --- accessors used by the panels ---
    StudioApp& Studio() { return studio_; }
    StudioProject& Project() { return studio_.Project(); }
    Engine& Preview() { return preview_; }
    EditorState& State() { return state_; }
    CommandStack& Commands() { return commands_; }
    const std::vector<LogLine>& Logs() const { return logs_; }

    void Log(const char* category, const std::string& msg, int severity = 0);
    void ClearLog() { logs_.clear(); }
    void MarkDirty() { state_.dirty = true; }
    void Select(SelKind k, int index);

    // --- actions (menu/toolbar/shortcuts route here) ---
    void NewProject();
    void OpenProject(const std::string& path);
    bool SaveProject();
    bool SaveProjectAs(const std::string& path);
    void BuildPreview();
    void StopPreview();

private:
    void DrawUI();
    void DrawMenuBar();
    void DrawToolbar();
    void HandleShortcuts();
    // Arrange all panels into Reverie's default docked layout. Runs on first launch (no saved
    // layout) and on View > Reset Layout. `dockspaceId` is an ImGuiID (kept as unsigned int so the
    // header stays ImGui-free). See EditorApp.cpp.
    void BuildDefaultLayout(unsigned int dockspaceId);
    bool resetLayout_ = false; // request a rebuild of the default layout next frame

    StudioApp studio_;
    Engine preview_;
    EditorState state_;
    CommandStack commands_;
    std::vector<LogLine> logs_;
};

} // namespace reverie::studio
