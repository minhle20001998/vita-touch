#pragma once
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <string>
#include "config.h"

enum class AppScreen { Touchpad, Settings };

// Which settings field the system keyboard is currently editing.
enum class ImeField { None, ServerIp, ServerPort, Passcode };

// Last-frame raw input snapshot, kept for the on-screen diagnostics.
struct InputState {
    SceCtrlData pad;
    SceCtrlData prevPad;
    SceTouchData touch;
    int touchRet = 0; // sceTouchPeek return value
    SceTouchPanelInfo panelInfo;
    bool panelInfoOk = false;
};

struct AppState {
    AppScreen screen = AppScreen::Touchpad;
    InputState input;
    Config config;
    bool scrollMode = false; // toggled by START in Phase 3; seeded from config
    ImeField imeField = ImeField::None;
    std::string testMsg;
    bool testOk = false;
};

// Edge-triggered button press (was up, now down).
inline bool pressed(const SceCtrlData& pad, const SceCtrlData& prev, unsigned btn) {
    return (pad.buttons & btn) && !(prev.buttons & btn);
}

// Edge-triggered button release (was down, now up).
inline bool released(const SceCtrlData& pad, const SceCtrlData& prev, unsigned btn) {
    return !(pad.buttons & btn) && (prev.buttons & btn);
}
