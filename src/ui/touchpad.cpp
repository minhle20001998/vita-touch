#include "../touchpad.h" // gesture engine namespace (src/touchpad.h)
#include "udp.h"
#include "ui/touchpad.h" // own drawTouchpad declaration
#include <imgui_vita2d/imgui.h>
#include <stdio.h>

#ifndef VITATOUCH_VERSION
#define VITATOUCH_VERSION "?"
#endif

// Draws the touchpad screen. The gesture engine (touchpad::update) is driven
// from main.cpp before this runs; here we only present state.
bool drawTouchpad(AppState& app) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(960, 544));
    ImGui::Begin("VitaTouchPad", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (app.scrollMode) {
        ImGui::TextColored(ImVec4(1, 0.65f, 0.2f, 1), "VITA TOUCHPAD  [SCROLL]   (v%s)",
                           VITATOUCH_VERSION);
    } else {
        ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "VITA TOUCHPAD  [CURSOR]   (v%s)",
                           VITATOUCH_VERSION);
    }

    // Everything below the title is debug-only. The daily screen is just the
    // title above plus the instructions at the bottom.
    if (app.config.showEventLog) {
        // --- connection line ---
        udp::Stats st = udp::stats();
        if (!udp::isUp()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "x network stack down");
        } else if (app.config.serverIp.empty() || !udp::hasDestination()) {
            ImGui::TextColored(ImVec4(1, 0.9f, 0.4f, 1),
                               "! not configured — press SELECT, set server IP");
        } else if (st.consecFails >= 5) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                               "x disconnected? send errors=%u (%s:%d)",
                               st.failed, app.config.serverIp.c_str(), app.config.serverPort);
        } else {
            ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "o %s:%d  (sent=%u failed=%u)",
                               app.config.serverIp.c_str(), app.config.serverPort,
                               st.sent, st.failed);
        }

        // --- gesture state ---
        ImGui::Text("fingers=%d phase=%s", touchpad::fingerCount(), touchpad::phaseName());
        ImGui::Separator();

        // --- recent event log (newest first) ---
        int n = touchpad::logCount();
        if (n == 0) {
            ImGui::TextDisabled("(touch the surface — events appear here and go to the PC)");
        }
        for (int i = 0; i < n; i++) {
            if (i == 0) {
                ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "> %s", touchpad::logLine(i));
            } else {
                ImGui::TextDisabled("  %s", touchpad::logLine(i));
            }
        }

        ImGui::Spacing();
    }

    ImGui::Separator();
    ImGui::TextDisabled("drag=move · tap=click · hold+drag · 2-finger=scroll · 2-tap=right-click");
    ImGui::TextDisabled("START=scroll mode · SELECT=settings");

    ImGui::End();
    return false;
}
