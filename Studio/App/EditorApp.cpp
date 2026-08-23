// Reverie/Studio/App/EditorApp.cpp - see EditorApp.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "EditorApp.h"

#include "Panels.h"
#include "StudioBackend.h"
#include "Theme.h"

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API for the default docked layout

#include <cstdio>
#include <cstring>

namespace reverie::studio {

namespace {
enum class Dialog { None, Open, SaveAs };
Dialog g_dialog = Dialog::None;
char g_pathBuf[512] = "";

StudioProject StarterProject() {
    StudioProject p;
    p.buses.push_back({"Combat", "Music", 0.9f, false, false});
    p.parameters.push_back({"CombatIntensity", 0.0f, 0.0f, 1.0f, 250.0f});
    return p;
}
} // namespace

void EditorApp::Log(const char* category, const std::string& msg, int severity) {
    logs_.push_back({category ? category : "Editor", msg, severity});
    if (logs_.size() > 2000) logs_.erase(logs_.begin());
}

void EditorApp::Select(SelKind k, int index) {
    state_.sel = k;
    state_.selIndex = index;
}

void EditorApp::NewProject() {
    const bool audio = state_.audioAvailable; // survives the state reset (device stays open)
    studio_.Project() = StarterProject();
    state_ = EditorState{}; // reset selection/dirty/panel-visibility to defaults
    state_.audioAvailable = audio;
    commands_.Clear();
    Log("Editor", "New project created.");
}

void EditorApp::OpenProject(const std::string& path) {
    if (studio_.Load(path)) {
        state_.projectPath = path;
        state_.dirty = false;
        state_.sel = SelKind::None;
        state_.selIndex = -1;
        commands_.Clear();
        Log("Editor", "Opened project: " + path);
    } else {
        Log("Editor", "Failed to open project: " + path, 2);
    }
}

bool EditorApp::SaveProject() {
    if (state_.projectPath.empty()) state_.projectPath = "untitled.rvproj";
    if (studio_.Save(state_.projectPath)) {
        state_.dirty = false;
        Log("Editor", "Saved project: " + state_.projectPath);
        return true;
    }
    Log("Editor", "Failed to save project: " + state_.projectPath, 2);
    return false;
}

bool EditorApp::SaveProjectAs(const std::string& path) {
    if (studio_.Save(path)) {
        state_.projectPath = path;
        state_.dirty = false;
        Log("Editor", "Saved project as: " + path);
        return true;
    }
    Log("Editor", "Failed to save project as: " + path, 2);
    return false;
}

void EditorApp::BuildPreview() {
    // Clean rebuild: reset the preview engine so repeated builds do not accumulate registrations.
    const Backend be = state_.audioAvailable ? Backend::Miniaudio : Backend::Null;
    preview_.Shutdown();
    Config ec;
    ec.backend = be;
    ec.sampleRate = 48000;
    ec.channels = 2;
    if (Failed(preview_.Init(ec))) {
        Log("Build", "Preview engine re-init failed.", 2);
        state_.built = false;
        return;
    }
    if (state_.audioAvailable) preview_.Start();
    studio_.Build(preview_);
    state_.built = true;
    const StudioProject& p = studio_.Project();
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Built preview: %zu assets, %zu buses, %zu params, %zu events, %zu music.",
                  p.assets.size(), p.buses.size(), p.parameters.size(), p.events.size(),
                  p.music.size());
    Log("Build", buf);
}

void EditorApp::StopPreview() {
    preview_.StopMusic();
    preview_.StopAll();
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) NewProject();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) { g_dialog = Dialog::Open; g_pathBuf[0] = 0; }
        if (ImGui::MenuItem("Save", "Ctrl+S")) SaveProject();
        if (ImGui::MenuItem("Save As...")) {
            g_dialog = Dialog::SaveAs;
            std::snprintf(g_pathBuf, sizeof(g_pathBuf), "%s", state_.projectPath.c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            // Request a graceful close by posting nothing here; the user closes the window.
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem(commands_.CanUndo() ? (std::string("Undo ") + commands_.UndoName()).c_str()
                                                : "Undo",
                            "Ctrl+Z", false, commands_.CanUndo()))
            commands_.Undo();
        if (ImGui::MenuItem(commands_.CanRedo() ? (std::string("Redo ") + commands_.RedoName()).c_str()
                                                : "Redo",
                            "Ctrl+Y", false, commands_.CanRedo()))
            commands_.Redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Asset Browser", nullptr, &state_.pAssets);
        ImGui::MenuItem("Inspector", nullptr, &state_.pInspector);
        ImGui::MenuItem("Mixer", nullptr, &state_.pMixer);
        ImGui::MenuItem("Events", nullptr, &state_.pEvents);
        ImGui::MenuItem("Music", nullptr, &state_.pMusic);
        ImGui::MenuItem("Profiler", nullptr, &state_.pProfiler);
        ImGui::MenuItem("Console", nullptr, &state_.pConsole);
        ImGui::MenuItem("Project Settings", nullptr, &state_.pProjectSettings);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout")) resetLayout_ = true;
        ImGui::MenuItem("ImGui Demo", nullptr, &state_.pDemo);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Build")) {
        if (ImGui::MenuItem("Build Preview", "Ctrl+B")) BuildPreview();
        if (ImGui::MenuItem("Stop Preview")) StopPreview();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Reverie Studio - Hollow Dream Studios", nullptr, false, false);
        ImGui::EndMenu();
    }
    // Right-aligned dirty / audio status.
    {
        char status[96];
        std::snprintf(status, sizeof(status), "%s  |  audio: %s%s",
                      state_.dirty ? "*unsaved" : "saved", state_.audioAvailable ? "on" : "null",
                      state_.built ? "  |  built" : "");
        const float w = ImGui::CalcTextSize(status).x + 20.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - w);
        ImGui::TextDisabled("%s", status);
    }
    ImGui::EndMainMenuBar();
}

// Draws the toolbar row's buttons into the CURRENT window (the host strip); no Begin/End of its own.
void EditorApp::DrawToolbar() {
    if (ImGui::Button("New")) NewProject();
    ImGui::SameLine();
    if (ImGui::Button("Open")) { g_dialog = Dialog::Open; g_pathBuf[0] = 0; }
    ImGui::SameLine();
    if (ImGui::Button("Save")) SaveProject();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::BeginDisabled(!commands_.CanUndo());
    if (ImGui::Button("Undo")) commands_.Undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!commands_.CanRedo());
    if (ImGui::Button("Redo")) commands_.Redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("Build")) BuildPreview();
    ImGui::SameLine();
    if (ImGui::Button("Play")) {
        if (!state_.built) BuildPreview();
        if (state_.sel == SelKind::Event && state_.selIndex >= 0 &&
            state_.selIndex < (int)Project().events.size()) {
            const EventId e = studio_.Event(Project().events[state_.selIndex].name);
            if (e != kInvalidId) preview_.PlayEvent(e, 1.0f);
        } else if (state_.sel == SelKind::Music && state_.selIndex >= 0 &&
                   state_.selIndex < (int)Project().music.size()) {
            const MusicStateId m = studio_.Music(Project().music[state_.selIndex].name);
            if (m != kInvalidId) preview_.SetMusicState(m);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) StopPreview();
}

// Arranges every panel into Reverie's default docked layout: a top toolbar strip (drawn by the host
// window, not docked), a left column (Asset Browser over Events), a wide central Mixer, a right
// Inspector/Project Settings tab group, and a bottom Music/Console/Profiler tab group.
void EditorApp::BuildDefaultLayout(unsigned int dockspaceId) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

    ImGuiID centerRight = 0, center = 0, centerTop = 0, leftTop = 0;
    const ImGuiID leftId = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.20f, nullptr, &centerRight);
    const ImGuiID rightId = ImGui::DockBuilderSplitNode(centerRight, ImGuiDir_Right, 0.25f, nullptr, &center);
    const ImGuiID bottomId = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &centerTop);
    const ImGuiID leftBottomId = ImGui::DockBuilderSplitNode(leftId, ImGuiDir_Down, 0.5f, nullptr, &leftTop);

    ImGui::DockBuilderDockWindow("Asset Browser", leftTop);
    ImGui::DockBuilderDockWindow("Events", leftBottomId);
    ImGui::DockBuilderDockWindow("Mixer", centerTop);
    ImGui::DockBuilderDockWindow("Music", bottomId);
    ImGui::DockBuilderDockWindow("Console", bottomId);
    ImGui::DockBuilderDockWindow("Profiler", bottomId);
    ImGui::DockBuilderDockWindow("Inspector", rightId);
    ImGui::DockBuilderDockWindow("Project Settings", rightId);
    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorApp::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return; // don't steal typing
    if (!io.KeyCtrl) return;
    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) NewProject();
    else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) { g_dialog = Dialog::Open; g_pathBuf[0] = 0; }
    else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveProject();
    else if (ImGui::IsKeyPressed(ImGuiKey_B, false)) BuildPreview();
    else if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) commands_.Undo();
    else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) commands_.Redo();
}

void EditorApp::DrawUI() {
    DrawMenuBar();

    // One full-work-area host window holds the toolbar strip and the dockspace, so the toolbar is a
    // fixed top strip (not a floating window) and panels dock edge-to-edge below it.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking;
    ImGui::Begin("ReverieHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    // Toolbar strip (its own child so it keeps normal padding inside the zero-padding host).
    const float stripH = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f + 4.0f;
    ImGui::BeginChild("##toolbarstrip", ImVec2(0.0f, stripH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawToolbar();
    ImGui::EndChild();

    // The dockspace fills the rest of the host. Build the default layout on first run (no saved
    // .ini layout) or when the user resets it; otherwise the persisted layout is honored.
    const ImGuiID dockId = ImGui::GetID("ReverieDockSpace");
    if (resetLayout_ || ImGui::DockBuilderGetNode(dockId) == nullptr) {
        BuildDefaultLayout(dockId);
        resetLayout_ = false;
    }
    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    HandleShortcuts();

    if (state_.pAssets) DrawAssetsPanel(*this);
    if (state_.pInspector) DrawInspectorPanel(*this);
    if (state_.pMixer) DrawMixerPanel(*this);
    if (state_.pEvents) DrawEventsPanel(*this);
    if (state_.pMusic) DrawMusicPanel(*this);
    if (state_.pProfiler) DrawProfilerPanel(*this);
    if (state_.pConsole) DrawConsolePanel(*this);
    if (state_.pProjectSettings) DrawProjectSettingsPanel(*this);
    if (state_.pDemo) ImGui::ShowDemoWindow(&state_.pDemo);

    // Open/Save-As path dialog (native file browser is a future platform-backend addition).
    if (g_dialog != Dialog::None) {
        ImGui::OpenPopup("ProjectPath");
        if (ImGui::BeginPopupModal("ProjectPath", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(g_dialog == Dialog::Open ? "Open project (path):"
                                                            : "Save project as (path):");
            ImGui::SetNextItemWidth(420.0f);
            ImGui::InputText("##path", g_pathBuf, sizeof(g_pathBuf));
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                if (g_dialog == Dialog::Open) OpenProject(g_pathBuf);
                else SaveProjectAs(g_pathBuf);
                g_dialog = Dialog::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                g_dialog = Dialog::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

int EditorApp::Run() {
    BackendConfig bc;
    bc.title = "Reverie Studio";
    auto backend = CreateStudioBackend(bc);
    if (!backend) {
        std::fprintf(stderr, "Reverie Studio: no GUI backend available on this platform/build.\n");
        return 1;
    }
    ApplyReverieTheme();

    // Preview engine: a real device if possible, otherwise a silent Null engine.
    {
        Config ec;
        ec.backend = Backend::Miniaudio;
        ec.sampleRate = 48000;
        ec.channels = 2;
        if (Failed(preview_.Init(ec))) {
            ec.backend = Backend::Null;
            preview_.Init(ec);
            state_.audioAvailable = false;
            Log("Runtime", "No audio device; preview is silent (Null backend).", 1);
        } else {
            state_.audioAvailable = true;
            preview_.Start();
            Log("Runtime", "Audio device ready for preview.");
        }
    }

    NewProject();
    Log("Editor", std::string("GUI backend: ") + backend->Name());

    const float clear[4] = {0.10f, 0.11f, 0.13f, 1.0f};
    while (backend->PumpEvents()) {
        preview_.Update(); // host pump (music quantized transitions)
        backend->BeginFrame();
        DrawUI();
        backend->EndFrame(clear);
    }

    preview_.Shutdown();
    return 0;
}

} // namespace reverie::studio
