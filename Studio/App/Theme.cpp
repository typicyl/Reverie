// Reverie/Studio/App/Theme.cpp - see Theme.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Theme.h"

#include "imgui.h"

namespace reverie::studio {

void ThemeColors::Accent(float out[4]) { out[0] = 0.26f; out[1] = 0.59f; out[2] = 0.98f; out[3] = 1.0f; }
void ThemeColors::Meter(float out[4]) { out[0] = 0.30f; out[1] = 0.78f; out[2] = 0.47f; out[3] = 1.0f; }
void ThemeColors::MeterHot(float out[4]) { out[0] = 0.90f; out[1] = 0.36f; out[2] = 0.28f; out[3] = 1.0f; }
void ThemeColors::GridLine(float out[4]) { out[0] = 1.0f; out[1] = 1.0f; out[2] = 1.0f; out[3] = 0.08f; }

void ApplyReverieTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.WindowPadding = ImVec2(8, 8);
    s.FramePadding = ImVec2(7, 4);
    s.ItemSpacing = ImVec2(8, 6);
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = s.Colors;
    const ImVec4 bg = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    const ImVec4 panel = ImVec4(0.15f, 0.16f, 0.19f, 1.00f);
    const ImVec4 panelHover = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    const ImVec4 accent = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    const ImVec4 text = ImVec4(0.90f, 0.91f, 0.93f, 1.00f);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.55f, 1.00f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.07f);
    c[ImGuiCol_FrameBg] = panel;
    c[ImGuiCol_FrameBgHovered] = panelHover;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
    c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.65f);
    c[ImGuiCol_Button] = panel;
    c[ImGuiCol_ButtonHovered] = panelHover;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_Separator] = c[ImGuiCol_Border];
    c[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered] = panelHover;
    c[ImGuiCol_TabActive] = ImVec4(0.20f, 0.30f, 0.44f, 1.00f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.19f, 0.24f, 1.00f);
    c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotLines] = ImVec4(0.60f, 0.75f, 0.95f, 1.00f);

    // When viewports are enabled, tone down the platform-window rounding so detached panels match.
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        s.WindowRounding = 4.0f;
        c[ImGuiCol_WindowBg].w = 1.0f;
    }
}

} // namespace reverie::studio
