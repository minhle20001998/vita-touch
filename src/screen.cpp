#include "screen.h"
#include <psp2/avconfig.h>

namespace screen {

namespace {
int g_max = 65536;
}

bool init() {
    int m = 0;
    if (sceAVConfigGetDisplayMaxBrightness(&m) == 0 && m > 0) g_max = m;
    return true;
}

void shutdown() { apply(FULL, 21); }

void apply(Level level, int dimLevel) {
    int v = g_max;
    if (level == DIM) {
        v = dimLevel;
        if (v < 21) v = 21; // 21 is the hardware floor; 0 turns the screen off
        if (v > g_max) v = g_max;
    } else if (level == OFF) {
        v = 0;
    }
    // Best-effort: brightness failure must never break the app.
    sceAVConfigSetDisplayBrightness(v);
}

void applyBattery(const Config& c) {
    apply(c.batteryMode == "off" ? OFF : DIM, c.dimLevel);
}

int maxBrightness() { return g_max; }

} // namespace screen
