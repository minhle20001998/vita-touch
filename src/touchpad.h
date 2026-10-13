#pragma once
#include "app.h"

namespace touchpad {
    // Entering the touchpad screen: clear gesture state (no stuck drags).
    void reset();
    // Leaving the screen / shutdown: release any held button (retried).
    void suspend(AppState& app);
    // Sample app.input.touch and emit UDP events. Call once per frame while
    // on the touchpad screen.
    void update(AppState& app);
    // Right-stick cursor drive: tilt direction at constant stickSpeed.
    // Always emits MOVE (both modes) — pairs with scroll mode. Call alongside update().
    void updateStick(AppState& app);

    const char* phaseName();
    int phaseId(); // numeric phase for measurement logging (matches Phase order)
    int fingerCount();

    // Recent event descriptions for the on-screen log (newest first).
    int logCount();
    const char* logLine(int i);
}
