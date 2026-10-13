#include "settings.h"
#include "config.h"
#include "ime.h"
#include "protocol.h"
#include "udp.h"
#include <imgui_vita2d/imgui.h>
#include <stdio.h>
#include <stdlib.h>

// Consumes a finished IME dialog into the pending settings field.
static void applyImeResult(AppState& app) {
    if (!ime::finished()) return;
    if (!ime::cancelled()) {
        std::string t = ime::text();
        if (app.imeField == ImeField::ServerIp) {
            if (config::validIpv4(t)) {
                app.config.serverIp = t;
                config::save(app.config);
            }
        } else if (app.imeField == ImeField::ServerPort) {
            int p = atoi(t.c_str());
            if (config::validPort(p)) {
                app.config.serverPort = p;
                config::save(app.config);
            }
        } else if (app.imeField == ImeField::Passcode) {
            if (t.size() <= 15) {
                app.config.passcode = t;
                config::save(app.config);
            }
        }
    }
    app.imeField = ImeField::None;
}

static void stepper(const char* id, float& value) {
    ImGui::PushID(id);
    if (ImGui::Button(" - ", ImVec2(56, 44))) {
        value -= 0.25f;
        if (value < 0.25f) value = 0.25f;
    }
    ImGui::SameLine();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", value);
    ImGui::Text("%s", buf);
    ImGui::SameLine();
    if (ImGui::Button(" + ", ImVec2(56, 44))) {
        value += 0.25f;
        if (value > 4.0f) value = 4.0f;
    }
    ImGui::PopID();
}

static void intStepper(const char* id, int& value, int lo, int hi, int step) {
    ImGui::PushID(id);
    if (ImGui::Button(" - ", ImVec2(56, 44))) {
        value -= step;
        if (value < lo) value = lo;
    }
    ImGui::SameLine();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    ImGui::Text("%s", buf);
    ImGui::SameLine();
    if (ImGui::Button(" + ", ImVec2(56, 44))) {
        value += step;
        if (value > hi) value = hi;
    }
    ImGui::PopID();
}

bool drawSettings(AppState& app) {
    applyImeResult(app);
    Config& cfg = app.config;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(960, 544));
    ImGui::Begin("VitaTouchPad Settings", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Text("SETTINGS  (full backlight while open)");
    ImGui::Separator();
    ImGui::TextDisabled("Tap a field, or navigate with D-pad + Cross.");

    // Scrollable form: the row count no longer fits 544px. Test/Back stay
    // pinned below the child.
    ImGui::BeginChild("settings_form", ImVec2(0, -116), false);

    // --- server IP ---
    ImGui::Text("Server IP:");
    ImGui::SameLine(220);
    if (ImGui::Button(cfg.serverIp.empty() ? "<not set>  ##ip" : (cfg.serverIp + "  ##ip").c_str(),
                      ImVec2(300, 44))) {
        if (ime::open("Server IP address", cfg.serverIp, 15)) {
            app.imeField = ImeField::ServerIp;
        }
    }
    if (!cfg.serverIp.empty() && !config::validIpv4(cfg.serverIp)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "invalid IP");
    }

    // --- port ---
    ImGui::Text("Port:");
    ImGui::SameLine(220);
    char portBuf[16];
    snprintf(portBuf, sizeof(portBuf), "%d", cfg.serverPort);
    if (ImGui::Button((std::string(portBuf) + "  ##port").c_str(), ImVec2(300, 44))) {
        if (ime::open("Server UDP port (1-65535)", portBuf, 5)) {
            app.imeField = ImeField::ServerPort;
        }
    }

    // --- passcode ---
    ImGui::Text("Passcode:");
    ImGui::SameLine(220);
    std::string masked = cfg.passcode.empty() ? "<none>  ##pass"
                                              : (std::string(cfg.passcode.size(), '*') + "  ##pass");
    if (ImGui::Button(masked.c_str(), ImVec2(300, 44))) {
        if (ime::open("Passcode (max 15 chars)", cfg.passcode, 15)) {
            app.imeField = ImeField::Passcode;
        }
    }

    ImGui::Spacing();

    // --- sensitivity ---
    ImGui::Text("Sensitivity:");
    ImGui::SameLine(220);
    float sensBefore = cfg.sensitivity;
    stepper("sens", cfg.sensitivity);
    if (cfg.sensitivity != sensBefore) config::save(cfg);

    ImGui::Text("Scroll sens:");
    ImGui::SameLine(220);
    float scrollBefore = cfg.scrollSensitivity;
    stepper("scrollsens", cfg.scrollSensitivity);
    if (cfg.scrollSensitivity != scrollBefore) config::save(cfg);

    ImGui::Text("Tap slop:");
    ImGui::SameLine(220);
    int slopBefore = cfg.tapSlop;
    intStepper("tapslop", cfg.tapSlop, 4, 64, 2);
    if (cfg.tapSlop != slopBefore) config::save(cfg);

    ImGui::Text("Hold time:");
    ImGui::SameLine(220);
    int holdBefore = cfg.holdMs;
    intStepper("holdms", cfg.holdMs, 100, 1000, 50);
    if (cfg.holdMs != holdBefore) config::save(cfg);
    ImGui::SameLine();
    ImGui::TextDisabled("ms to start drag");

    ImGui::Text("Stick speed:");
    ImGui::SameLine(220);
    int stickBefore = cfg.stickSpeed;
    intStepper("stickspeed", cfg.stickSpeed, 120, 2400, 60);
    if (cfg.stickSpeed != stickBefore) config::save(cfg);
    ImGui::SameLine();
    ImGui::TextDisabled("u/s");

    ImGui::Text("Swipe dist:");
    ImGui::SameLine(220);
    int swipeBefore = cfg.swipeDist;
    intStepper("swipedist", cfg.swipeDist, 40, 400, 10);
    if (cfg.swipeDist != swipeBefore) config::save(cfg);
    ImGui::SameLine();
    ImGui::TextDisabled("3-f swipe");

    ImGui::Text("Pinch slop:");
    ImGui::SameLine(220);
    int pinchBefore = cfg.pinchSlop;
    intStepper("pinchslop", cfg.pinchSlop, 16, 120, 4);
    if (cfg.pinchSlop != pinchBefore) config::save(cfg);
    ImGui::SameLine();
    ImGui::TextDisabled("zoom lock");

    ImGui::Text("Land hold:");
    ImGui::SameLine(220);
    int landBefore = cfg.landingHoldMs;
    intStepper("landhold", cfg.landingHoldMs, 0, 200, 20);
    if (cfg.landingHoldMs != landBefore) config::save(cfg);
    ImGui::SameLine();
    ImGui::TextDisabled("ms anti-teleport");

    ImGui::Spacing();

    // --- battery mode ---
    ImGui::Text("Battery mode:");
    ImGui::SameLine(220);
    bool isDim = cfg.batteryMode != "off";
    if (ImGui::Button(isDim ? "(*) Dim" : "( ) Dim", ImVec2(140, 44))) {
        cfg.batteryMode = "dim";
        config::save(cfg);
    }
    ImGui::SameLine();
    if (ImGui::Button(!isDim ? "(*) Off" : "( ) Off", ImVec2(140, 44))) {
        cfg.batteryMode = "off";
        config::save(cfg);
    }

    // --- start in scroll mode ---
    char scrollLabel[64];
    snprintf(scrollLabel, sizeof(scrollLabel), "%s Start in scroll mode",
             cfg.startInScrollMode ? "[x]" : "[ ]");
    if (ImGui::Button(scrollLabel, ImVec2(300, 44))) {
        cfg.startInScrollMode = !cfg.startInScrollMode;
        config::save(cfg);
    }

    // --- event feed (debug) ---
    char logLabel[64];
    snprintf(logLabel, sizeof(logLabel), "%s Event feed (debug)",
             cfg.showEventLog ? "[x]" : "[ ]");
    if (ImGui::Button(logLabel, ImVec2(300, 44))) {
        cfg.showEventLog = !cfg.showEventLog;
        config::save(cfg);
    }

    // --- measurement recording (applies on next launch: one file per boot) ---
    char measureLabel[64];
    snprintf(measureLabel, sizeof(measureLabel), "%s Record measurements",
             cfg.measureMode ? "[x]" : "[ ]");
    if (ImGui::Button(measureLabel, ImVec2(300, 44))) {
        cfg.measureMode = !cfg.measureMode;
        config::save(cfg);
    }

    ImGui::Spacing();

    ImGui::EndChild();

    // --- test + back ---
    if (ImGui::Button("Send test HELLO", ImVec2(300, 48))) {
        if (!udp::isUp()) {
            app.testMsg = "network stack not up";
            app.testOk = false;
        } else if (!udp::setDestination(cfg.serverIp, cfg.serverPort)) {
            app.testMsg = "bad IP/port — fix above first";
            app.testOk = false;
        } else {
            uint8_t mode = app.scrollMode ? vtp::MODE_SCROLL : vtp::MODE_CURSOR;
            bool ok = udp::sendHello(mode, cfg.startInScrollMode, cfg.passcode);
            udp::Stats st = udp::stats();
            char buf[96];
            snprintf(buf, sizeof(buf), ok ? "HELLO sent (sent=%u failed=%u)"
                                          : "send failed (sent=%u failed=%u)",
                     st.sent, st.failed);
            app.testMsg = buf;
            app.testOk = ok;
        }
    }
    if (!app.testMsg.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(app.testOk ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1),
                           "%s", app.testMsg.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Back to touchpad (Circle)", ImVec2(320, 48))) {
        app.screen = AppScreen::Touchpad;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("SELECT also closes");

    ImGui::End();
    return false;
}
