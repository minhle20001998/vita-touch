#pragma once
#include "config.h"

namespace screen {
    enum Level { FULL, DIM, OFF };

    // Queries the max brightness (falls back to 65536 on error).
    bool init();
    // Restores full brightness.
    void shutdown();
    void apply(Level level, int dimLevel);
    void applyBattery(const Config& c);
    int maxBrightness();
}
