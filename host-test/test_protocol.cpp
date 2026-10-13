// Host test: builds every packet type via protocol.h and dumps hex for
// the Python checker to decode per planning/04-wire-protocol.md.
#include <stdio.h>
#include "protocol.h"

static void dump(const char* name, const uint8_t* buf, size_t n) {
    printf("%s %zu ", name, n);
    for (size_t i = 0; i < n; i++) printf("%02x", buf[i]);
    printf("\n");
}

int main() {
    uint8_t buf[vtp::MAX_PACKET];
    dump("HELLO", buf, vtp::buildHello(buf, vtp::MODE_CURSOR, true, 7, 123456, "secret"));
    dump("MOVE", buf, vtp::buildMove(buf, -300, 1024, 8, 123500, "secret"));
    dump("BUTTON", buf, vtp::buildButton(buf, vtp::BUTTON_LEFT, vtp::ACTION_DOWN, 10, 123700, "secret"));
    dump("MODE", buf, vtp::buildMode(buf, vtp::MODE_SCROLL, 11, 123800, "secret"));
    dump("PING", buf, vtp::buildPing(buf, 12, 123900, "secret"));
    // v2 neutral motion (replaces v1 SCROLL)
    dump("FMOVE", buf, vtp::buildFMove(buf, 2, 0, -120, 9, 123600, "secret"));
    // discrete swipe + pinch spread
    dump("SWIPE", buf, vtp::buildSwipe(buf, 3, vtp::SWIPE_LEFT, 16, 124300, "secret"));
    dump("ZOOM", buf, vtp::buildZoom(buf, 2, -60, 17, 124400, "secret"));
    // long passcode truncation (16+ chars -> first 16 bytes kept)
    dump("HELLO_LONGPC", buf, vtp::buildHello(buf, vtp::MODE_CURSOR, false, 13, 124000,
                                              "0123456789abcdefXYZ"));
    // empty passcode -> zeroed field
    dump("PING_EMPTYPC", buf, vtp::buildPing(buf, 14, 124100, ""));
    // vita pad button edge
    dump("VPAD", buf, vtp::buildVPad(buf, vtp::VITA_TRIANGLE, vtp::ACTION_DOWN, 15, 124200, "secret"));
    // motion-bout terminator
    dump("END", buf, vtp::buildEnd(buf, vtp::FMOVE, 2, 18, 124500, "secret"));
    return 0;
}
