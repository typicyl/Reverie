// Reverie/Studio/App/Panels.cpp - see Panels.h. The editor's dockable views over EditorApp.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Panels read the authoring model + preview engine off EditorApp and route edits through the command
// stack (undo/redo). Meters and the music timeline use custom draw-list rendering; everything else
// uses standard ImGui widgets. Every visible control either works or is clearly marked "planned".
#include "Panels.h"

#include "EditorApp.h"
#include "Theme.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace reverie::studio {

namespace {

float g_dragOld = 0.0f; // pre-drag value for single-undo-per-drag (one active slider at a time)

// A float slider that records ONE undo command per drag (on release). `field` is edited live;
// `setByValue` applies a value for redo/undo (captures the target index).
void DragUndoFloat(EditorApp& app, const char* label, float* field, float vmin, float vmax,
                   const char* cmdName, std::function<void(float)> setByValue) {
    const float before = *field;
    ImGui::SliderFloat(label, field, vmin, vmax);
    if (ImGui::IsItemActivated()) g_dragOld = before;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const float oldv = g_dragOld;
        const float newv = *field;
        app.Commands().Execute(cmdName, [setByValue, newv] { setByValue(newv); },
                               [setByValue, oldv] { setByValue(oldv); });
        app.MarkDirty();
    }
}

// A combo to pick an asset key from the project (returns true if changed). "" allowed as <none>.
bool AssetCombo(EditorApp& app, const char* label, std::string* key) {
    bool changed = false;
    const char* preview = key->empty() ? "<none>" : key->c_str();
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("<none>", key->empty())) { *key = ""; changed = true; }
        for (const StudioAsset& a : app.Project().assets)
            if (ImGui::Selectable(a.key.c_str(), *key == a.key)) { *key = a.key; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

bool ParamCombo(EditorApp& app, const char* label, std::string* name) {
    bool changed = false;
    const char* preview = name->empty() ? "<none>" : name->c_str();
    if (ImGui::BeginCombo(label, preview)) {
        if (ImGui::Selectable("<none>", name->empty())) { *name = ""; changed = true; }
        for (const StudioParam& p : app.Project().parameters)
            if (ImGui::Selectable(p.name.c_str(), *name == p.name)) { *name = p.name; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

// ---------------------------------------------------------------------------------- Asset Browser
void DrawAssetsPanel(EditorApp& app) {
    if (!ImGui::Begin("Asset Browser")) { ImGui::End(); return; }
    StudioProject& p = app.Project();

    static std::string newKey, newPath;
    ImGui::TextDisabled("Add an audio asset (import via native dialog is planned):");
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("key", &newKey);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("path", &newPath);
    ImGui::SameLine();
    if (ImGui::Button("Add") && !newKey.empty()) {
        StudioAsset a{newKey, newPath};
        app.Commands().Execute(
            "Add Asset", [&p, a] { p.assets.push_back(a); },
            [&p] { if (!p.assets.empty()) p.assets.pop_back(); });
        app.MarkDirty();
        newKey.clear();
        newPath.clear();
    }
    ImGui::Separator();

    if (ImGui::BeginTable("assets", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Source Path");
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)p.assets.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool selected = app.State().sel == SelKind::Asset && app.State().selIndex == i;
            if (ImGui::Selectable(p.assets[i].key.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns))
                app.Select(SelKind::Asset, i);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) {
                    const int idx = i;
                    const StudioAsset a = p.assets[i];
                    app.Commands().Execute(
                        "Delete Asset",
                        [&p, idx] { if (idx < (int)p.assets.size()) p.assets.erase(p.assets.begin() + idx); },
                        [&p, idx, a] {
                            p.assets.insert(p.assets.begin() + (idx <= (int)p.assets.size() ? idx : (int)p.assets.size()), a);
                        });
                    app.MarkDirty();
                }
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.assets[i].path.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// -------------------------------------------------------------------------------------- Inspector
void DrawInspectorPanel(EditorApp& app) {
    if (!ImGui::Begin("Inspector")) { ImGui::End(); return; }
    StudioProject& p = app.Project();
    const EditorState& st = app.State();

    switch (st.sel) {
    case SelKind::Asset:
        if (st.selIndex >= 0 && st.selIndex < (int)p.assets.size()) {
            StudioAsset& a = p.assets[st.selIndex];
            ImGui::SeparatorText("Audio Asset");
            if (ImGui::InputText("Key", &a.key)) app.MarkDirty();
            if (ImGui::InputText("Path", &a.path)) app.MarkDirty();
        }
        break;
    case SelKind::Bus:
        if (st.selIndex >= 0 && st.selIndex < (int)p.buses.size()) {
            StudioBus& b = p.buses[st.selIndex];
            const int idx = st.selIndex;
            ImGui::SeparatorText("Bus");
            if (ImGui::InputText("Name", &b.name)) app.MarkDirty();
            if (ImGui::InputText("Parent", &b.parent)) app.MarkDirty();
            DragUndoFloat(app, "Gain", &b.gain, 0.0f, 1.5f, "Bus Gain",
                          [&p, idx](float v) { p.buses[idx].gain = v; });
            if (ImGui::Checkbox("Muted", &b.muted)) app.MarkDirty();
            ImGui::SameLine();
            if (ImGui::Checkbox("Soloed", &b.soloed)) app.MarkDirty();
        }
        break;
    case SelKind::Param:
        if (st.selIndex >= 0 && st.selIndex < (int)p.parameters.size()) {
            StudioParam& pr = p.parameters[st.selIndex];
            ImGui::SeparatorText("Parameter (RTPC)");
            if (ImGui::InputText("Name", &pr.name)) app.MarkDirty();
            if (ImGui::DragFloat("Default", &pr.defaultValue, 0.01f)) app.MarkDirty();
            if (ImGui::DragFloat("Min", &pr.minValue, 0.01f)) app.MarkDirty();
            if (ImGui::DragFloat("Max", &pr.maxValue, 0.01f)) app.MarkDirty();
            if (ImGui::DragFloat("Smooth (ms)", &pr.smoothMs, 1.0f, 0.0f, 5000.0f)) app.MarkDirty();
            if (app.State().built) {
                const ParameterId id = app.Studio().Param(pr.name);
                if (id != kInvalidId) {
                    float live = app.Preview().ParameterValue(id);
                    ImGui::SeparatorText("Live");
                    if (ImGui::SliderFloat("Value", &live, pr.minValue, pr.maxValue))
                        app.Preview().SetParameter(id, live); // preview-only drive
                }
            }
        }
        break;
    case SelKind::Event:
        if (st.selIndex >= 0 && st.selIndex < (int)p.events.size()) {
            StudioEvent& e = p.events[st.selIndex];
            ImGui::SeparatorText("Event");
            if (ImGui::InputText("Name", &e.name)) app.MarkDirty();
            if (ImGui::InputInt("Priority", &e.priority)) app.MarkDirty();
            int maxI = (int)e.maxInstances;
            if (ImGui::InputInt("Max Instances (0=inf)", &maxI)) { e.maxInstances = (u32)(maxI < 0 ? 0 : maxI); app.MarkDirty(); }
            ImGui::SeparatorText("Layers");
            for (int li = 0; li < (int)e.layers.size(); ++li) {
                ImGui::PushID(li);
                StudioEventLayer& l = e.layers[li];
                ImGui::Text("Layer %d", li);
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) { e.layers.erase(e.layers.begin() + li); app.MarkDirty(); ImGui::PopID(); break; }
                if (AssetCombo(app, "Sound", &l.soundKey)) app.MarkDirty();
                if (ImGui::InputText("Bus", &l.bus)) app.MarkDirty();
                if (ImGui::SliderFloat("Volume", &l.volume, 0.0f, 1.5f)) app.MarkDirty();
                if (ImGui::SliderFloat("Vol var", &l.volumeVariance, 0.0f, 1.0f)) app.MarkDirty();
                if (ImGui::SliderFloat("Pitch", &l.pitch, 0.25f, 4.0f)) app.MarkDirty();
                if (ImGui::Checkbox("Loop", &l.loop)) app.MarkDirty();
                if (ParamCombo(app, "Gain param", &l.gainParam)) app.MarkDirty();
                ImGui::Separator();
                ImGui::PopID();
            }
            if (ImGui::Button("Add Layer")) { e.layers.push_back(StudioEventLayer{}); app.MarkDirty(); }
            if (app.State().built) {
                ImGui::SeparatorText("Preview");
                if (ImGui::Button("Play Event")) {
                    const EventId id = app.Studio().Event(e.name);
                    if (id != kInvalidId) app.Preview().PlayEvent(id, 1.0f);
                }
            }
        }
        break;
    case SelKind::Music:
        if (st.selIndex >= 0 && st.selIndex < (int)p.music.size()) {
            StudioMusicState& m = p.music[st.selIndex];
            ImGui::SeparatorText("Music State");
            if (ImGui::InputText("Name", &m.name)) app.MarkDirty();
            if (ImGui::DragFloat("BPM", &m.bpm, 1.0f, 20.0f, 400.0f)) app.MarkDirty();
            int bpb = (int)m.beatsPerBar;
            if (ImGui::InputInt("Beats/Bar", &bpb)) { m.beatsPerBar = (u32)(bpb < 1 ? 1 : bpb); app.MarkDirty(); }
            ImGui::SeparatorText("Layers");
            for (int li = 0; li < (int)m.layers.size(); ++li) {
                ImGui::PushID(li);
                StudioMusicLayer& l = m.layers[li];
                ImGui::Text("Layer %d", li);
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) { m.layers.erase(m.layers.begin() + li); app.MarkDirty(); ImGui::PopID(); break; }
                if (AssetCombo(app, "Sound", &l.soundKey)) app.MarkDirty();
                if (ImGui::SliderFloat("Gain", &l.gain, 0.0f, 1.5f)) app.MarkDirty();
                if (ParamCombo(app, "Gain param", &l.gainParam)) app.MarkDirty();
                if (ImGui::SliderFloat("Param lo", &l.paramLo, 0.0f, 1.0f)) app.MarkDirty();
                if (ImGui::SliderFloat("Param hi", &l.paramHi, 0.0f, 1.0f)) app.MarkDirty();
                ImGui::Separator();
                ImGui::PopID();
            }
            if (ImGui::Button("Add Layer")) { m.layers.push_back(StudioMusicLayer{}); app.MarkDirty(); }
        }
        break;
    case SelKind::None:
    default:
        ImGui::TextDisabled("Select an asset, bus, parameter, event, or music state.");
        break;
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------------------ Mixer
static void DrawMeter(float level, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(20, 22, 26, 255), 2.0f);
    if (level > 1.0f) level = 1.0f;
    const float fill = height * level;
    float g[4], hot[4];
    ThemeColors::Meter(g);
    ThemeColors::MeterHot(hot);
    const bool clip = level > 0.9f;
    const ImU32 col = clip ? IM_COL32((int)(hot[0] * 255), (int)(hot[1] * 255), (int)(hot[2] * 255), 255)
                           : IM_COL32((int)(g[0] * 255), (int)(g[1] * 255), (int)(g[2] * 255), 255);
    dl->AddRectFilled(ImVec2(pos.x, pos.y + height - fill), ImVec2(pos.x + width, pos.y + height), col,
                      2.0f);
    ImGui::Dummy(ImVec2(width, height));
}

void DrawMixerPanel(EditorApp& app) {
    if (!ImGui::Begin("Mixer")) { ImGui::End(); return; }
    StudioProject& p = app.Project();
    const bool built = app.State().built;

    ImGui::TextDisabled("Channel strips (project buses). Meters are live while previewing.");
    ImGui::Separator();

    for (int i = 0; i < (int)p.buses.size(); ++i) {
        StudioBus& b = p.buses[i];
        ImGui::PushID(i);
        ImGui::BeginGroup();
        const bool selected = app.State().sel == SelKind::Bus && app.State().selIndex == i;
        if (ImGui::Selectable(b.name.c_str(), selected, 0, ImVec2(90, 0))) app.Select(SelKind::Bus, i);

        float meter = 0.0f;
        if (built) {
            const BusId id = app.Studio().Bus(b.name);
            if (id != kInvalidId) meter = app.Preview().BusMeter(id);
        }
        DrawMeter(meter, 20.0f, 120.0f);
        ImGui::SameLine();
        DragUndoFloat(app, "##g", &b.gain, 0.0f, 1.5f, "Bus Gain",
                      [&p, i](float v) { p.buses[i].gain = v; });
        if (ImGui::Checkbox("M", &b.muted)) app.MarkDirty();
        ImGui::SameLine();
        if (ImGui::Checkbox("S", &b.soloed)) app.MarkDirty();
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (!built) ImGui::TextDisabled("Press Build to preview meters and hear audio.");
    ImGui::End();
}

// ---------------------------------------------------------------------------------------- Console
void DrawConsolePanel(EditorApp& app) {
    if (!ImGui::Begin("Console")) { ImGui::End(); return; }
    if (ImGui::Button("Clear")) app.ClearLog();
    ImGui::SameLine();
    static ImGuiTextFilter filter;
    filter.Draw("filter", 180);
    ImGui::Separator();
    if (ImGui::BeginChild("log")) {
        for (const LogLine& l : app.Logs()) {
            char line[600];
            std::snprintf(line, sizeof(line), "[%s] %s", l.category.c_str(), l.message.c_str());
            if (!filter.PassFilter(line)) continue;
            ImVec4 col = ImVec4(0.85f, 0.86f, 0.88f, 1.0f);
            if (l.severity == 1) col = ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
            else if (l.severity == 2) col = ImVec4(0.95f, 0.45f, 0.40f, 1.0f);
            ImGui::TextColored(col, "%s", line);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------------------- Events
void DrawEventsPanel(EditorApp& app) {
    if (!ImGui::Begin("Events")) { ImGui::End(); return; }
    StudioProject& p = app.Project();
    static std::string newName;
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##evtname", &newName);
    ImGui::SameLine();
    if (ImGui::Button("Add Event") && !newName.empty()) {
        StudioEvent e;
        e.name = newName;
        app.Commands().Execute(
            "Add Event", [&p, e] { p.events.push_back(e); },
            [&p] { if (!p.events.empty()) p.events.pop_back(); });
        app.MarkDirty();
        newName.clear();
    }
    ImGui::Separator();
    for (int i = 0; i < (int)p.events.size(); ++i) {
        const bool selected = app.State().sel == SelKind::Event && app.State().selIndex == i;
        if (ImGui::Selectable(p.events[i].name.c_str(), selected)) app.Select(SelKind::Event, i);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Delete")) {
                const int idx = i;
                const StudioEvent e = p.events[i];
                app.Commands().Execute(
                    "Delete Event",
                    [&p, idx] { if (idx < (int)p.events.size()) p.events.erase(p.events.begin() + idx); },
                    [&p, idx, e] { p.events.insert(p.events.begin() + (idx <= (int)p.events.size() ? idx : (int)p.events.size()), e); });
                app.MarkDirty();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

// ----------------------------------------------------------------------------------------- Music
void DrawMusicPanel(EditorApp& app) {
    if (!ImGui::Begin("Music")) { ImGui::End(); return; }
    StudioProject& p = app.Project();
    static std::string newName;
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##musname", &newName);
    ImGui::SameLine();
    if (ImGui::Button("Add State") && !newName.empty()) {
        StudioMusicState m;
        m.name = newName;
        app.Commands().Execute(
            "Add Music State", [&p, m] { p.music.push_back(m); },
            [&p] { if (!p.music.empty()) p.music.pop_back(); });
        app.MarkDirty();
        newName.clear();
    }
    ImGui::Separator();

    for (int i = 0; i < (int)p.music.size(); ++i) {
        const bool selected = app.State().sel == SelKind::Music && app.State().selIndex == i;
        if (ImGui::Selectable(p.music[i].name.c_str(), selected)) app.Select(SelKind::Music, i);
    }

    // Transport + a simple custom-rendered bars/beats timeline for the selected state.
    const EditorState& st = app.State();
    if (st.sel == SelKind::Music && st.selIndex >= 0 && st.selIndex < (int)p.music.size()) {
        StudioMusicState& m = p.music[st.selIndex];
        ImGui::SeparatorText(("Timeline: " + m.name).c_str());
        if (app.State().built) {
            if (ImGui::Button("Play")) {
                const MusicStateId id = app.Studio().Music(m.name);
                if (id != kInvalidId) app.Preview().SetMusicState(id);
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) app.Preview().StopMusic();
            ImGui::SameLine();
            ImGui::Text("beat %.2f  bar %llu  @ %.0f BPM", app.Preview().MusicBeat(),
                        (unsigned long long)app.Preview().MusicBar(), app.Preview().MusicBpm());
        } else {
            ImGui::TextDisabled("Build to preview music.");
        }

        // Custom timeline: bar grid + one row per layer (read-only visualization).
        const int bars = 8;
        const float rowH = 22.0f;
        const float barW = 90.0f;
        const float labelW = 90.0f;
        const int rows = (int)m.layers.size();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = labelW + bars * barW;
        const float height = (rows + 1) * rowH + 8.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float grid[4];
        ThemeColors::GridLine(grid);
        const ImU32 gridCol = IM_COL32((int)(grid[0] * 255), (int)(grid[1] * 255), (int)(grid[2] * 255),
                                       (int)(grid[3] * 255));
        // ruler
        for (int bar = 0; bar <= bars; ++bar) {
            const float x = origin.x + labelW + bar * barW;
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), gridCol);
            if (bar < bars) {
                char lbl[8];
                std::snprintf(lbl, sizeof(lbl), "%d", bar + 1);
                dl->AddText(ImVec2(x + 4, origin.y + 2), IM_COL32(150, 155, 165, 255), lbl);
            }
        }
        float accent[4];
        ThemeColors::Accent(accent);
        const ImU32 clipCol = IM_COL32((int)(accent[0] * 255), (int)(accent[1] * 255),
                                       (int)(accent[2] * 255), 150);
        for (int r = 0; r < rows; ++r) {
            const float y = origin.y + (r + 1) * rowH + 2.0f;
            dl->AddText(ImVec2(origin.x, y + 2), IM_COL32(200, 205, 215, 255),
                        m.layers[r].soundKey.empty() ? "<layer>" : m.layers[r].soundKey.c_str());
            dl->AddRectFilled(ImVec2(origin.x + labelW, y), ImVec2(origin.x + width, y + rowH - 6.0f),
                              clipCol, 3.0f); // a looping layer spans the arrangement
        }
        ImGui::Dummy(ImVec2(width, height));
        ImGui::TextDisabled("Timeline is a read-only visualization; clip/region editing is planned.");
    }
    ImGui::End();
}

// --------------------------------------------------------------------------------------- Profiler
void DrawProfilerPanel(EditorApp& app) {
    if (!ImGui::Begin("Profiler")) { ImGui::End(); return; }
    const EngineStats s = app.Preview().GetStats();
    ImGui::Text("Voices: %u active  (%u real / %u virtual)", s.activeVoices, s.realVoices,
                s.virtualVoices);
    ImGui::Text("Audio CPU: %.1f%%", s.cpuLoad * 100.0f);
    ImGui::Text("Master peak: %.3f", s.masterPeak);
    ImGui::Text("Music: beat %.2f  bar %llu  @ %.0f BPM", s.musicBeat,
                (unsigned long long)s.musicBar, s.musicBpm);
    ImGui::Text("Output: %u Hz, %u ch  |  device: %s", s.sampleRate, s.channels,
                app.State().audioAvailable ? "active" : "null");

    static float cpuHist[120] = {0};
    static int cpuPos = 0;
    cpuHist[cpuPos] = s.cpuLoad * 100.0f;
    cpuPos = (cpuPos + 1) % IM_ARRAYSIZE(cpuHist);
    ImGui::PlotLines("CPU %", cpuHist, IM_ARRAYSIZE(cpuHist), cpuPos, nullptr, 0.0f, 100.0f,
                     ImVec2(0, 60));
    if (!app.State().audioAvailable)
        ImGui::TextDisabled("No audio device: stats update only while rendering.");
    ImGui::End();
}

// -------------------------------------------------------------------------------- Project Settings
void DrawProjectSettingsPanel(EditorApp& app) {
    if (!ImGui::Begin("Project Settings")) { ImGui::End(); return; }
    const StudioProject& p = app.Project();
    ImGui::Text("Path: %s", app.State().projectPath.empty() ? "(unsaved)" : app.State().projectPath.c_str());
    ImGui::Separator();
    ImGui::Text("Assets: %zu", p.assets.size());
    ImGui::Text("Buses: %zu", p.buses.size());
    ImGui::Text("Parameters: %zu", p.parameters.size());
    ImGui::Text("Events: %zu", p.events.size());
    ImGui::Text("Music states: %zu", p.music.size());
    ImGui::Separator();
    ImGui::TextDisabled("Project-global settings (platforms, streaming thresholds, output format) "
                        "are planned.");
    ImGui::End();
}

} // namespace reverie::studio
