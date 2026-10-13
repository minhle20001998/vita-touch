#pragma once
#include <psp2/touch.h>
#include <stdint.h>

namespace measure {
    // Opens ux0:data/VitaTouchPad/measure_<N>.csv (N from a counter file) and
    // writes the header row. False on any I/O failure (caller continues
    // unrecorded). Evaluated at boot only: one launch = one labeled file.
    bool init();
    // Appends one row for a frame with contacts (n==0 frames are skipped;
    // lifts are inferred from time gaps in analysis). Per-frame write so a
    // force-close loses nothing. No-op when not recording.
    void logFrame(uint32_t tMs, const SceTouchData& touch, int phaseId);
    void shutdown();
}
