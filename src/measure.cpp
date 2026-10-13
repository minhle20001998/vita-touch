#include "measure.h"
#include "config.h"
#include <psp2/io/fcntl.h>
#include <stdio.h>
#include <string.h>

namespace measure {

namespace {
int g_fd = -1;
const int MAX_SLOTS = 4; // gestures never use more; extra reports dropped
} // namespace

bool init() {
    if (g_fd >= 0) return true;

    // Next file number, persisted across launches.
    int n = 1;
    {
        SceUID fd = sceIoOpen("ux0:data/VitaTouchPad/measure_next.txt", SCE_O_RDONLY, 0);
        if (fd >= 0) {
            char buf[16];
            int r = sceIoRead(fd, buf, sizeof(buf) - 1);
            sceIoClose(fd);
            if (r > 0) {
                buf[r] = '\0';
                int v = 0;
                sscanf(buf, "%d", &v);
                if (v >= 1) n = v;
            }
        }
    }

    char path[64];
    snprintf(path, sizeof(path), "ux0:data/VitaTouchPad/measure_%d.csv", n);
    g_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (g_fd < 0) return false;

    const char* header =
        "t_ms,n,"
        "id0,x0,y0,f0,id1,x1,y1,f1,id2,x2,y2,f2,id3,x3,y3,f3,"
        "phase\n";
    sceIoWrite(g_fd, header, strlen(header));

    // Bump the counter (best effort; a stale counter just reuses names).
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", n + 1);
        SceUID fd = sceIoOpen("ux0:data/VitaTouchPad/measure_next.txt",
                              SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (fd >= 0) {
            sceIoWrite(fd, buf, strlen(buf));
            sceIoClose(fd);
        }
    }
    return true;
}

void logFrame(uint32_t tMs, const SceTouchData& touch, int phaseId) {
    if (g_fd < 0) return;
    unsigned n = touch.reportNum;
    if (n == 0) return;
    if (n > (unsigned)MAX_SLOTS) n = MAX_SLOTS;

    char row[256];
    int o = snprintf(row, sizeof(row), "%u,%u", tMs, n);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (i < (int)n) {
            const SceTouchReport& r = touch.report[i];
            o += snprintf(row + o, sizeof(row) - o, ",%u,%d,%d,%u",
                          r.id, r.x, r.y, r.force);
        } else {
            // Four empty fields (id,x,y,force) for unused slots.
            o += snprintf(row + o, sizeof(row) - o, ",,,,");
        }
    }
    o += snprintf(row + o, sizeof(row) - o, ",%d\n", phaseId);
    sceIoWrite(g_fd, row, strlen(row));
}

void shutdown() {
    if (g_fd >= 0) {
        sceIoClose(g_fd);
        g_fd = -1;
    }
}

} // namespace measure
