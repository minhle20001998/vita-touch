#pragma once
// VitaTouchPad wire protocol v2 — header-only, no Vita dependencies.
// Byte layout is the contract in planning/04-wire-protocol.md.
//
// v2 changes: SCROLL is gone, replaced by the neutral FMOVE motion event
// (fingers + delta, no presumed meaning); discrete SWIPE and continuous
// ZOOM channels added. Servers must reject version != 2.

#include <stdint.h>
#include <string.h>

namespace vtp {

enum Type : uint8_t {
    HELLO  = 0x01,
    MOVE   = 0x02,
    FMOVE  = 0x03, // N-finger motion: {fingers, dx, dy} (replaces v1 SCROLL)
    BUTTON = 0x04,
    MODE   = 0x05,
    PING   = 0x06,
    VPAD   = 0x07, // Vita gamepad button edge (press/release), for PC shortcuts
    SWIPE  = 0x08, // discrete directional swipe: {fingers, direction}
    ZOOM   = 0x09, // pinch spread stream: {fingers, dSpread}
    END    = 0x0A, // motion-bout terminator: {ended_type, fingers}
};

enum Button : uint8_t {
    BUTTON_LEFT   = 1,
    BUTTON_RIGHT  = 2,
    BUTTON_MIDDLE = 3,
};

enum ButtonAction : uint8_t {
    ACTION_UP   = 0,
    ACTION_DOWN = 1,
};

// VPAD button codes (Vita controls; SELECT/START are local-only, never sent).
enum VPadButton : uint8_t {
    VITA_CROSS    = 1,
    VITA_CIRCLE   = 2,
    VITA_SQUARE   = 3,
    VITA_TRIANGLE = 4,
    VITA_L1       = 5,
    VITA_R1       = 6,
    VITA_DPAD_UP    = 7,
    VITA_DPAD_DOWN  = 8,
    VITA_DPAD_LEFT  = 9,
    VITA_DPAD_RIGHT = 10,
};

enum Mode : uint8_t {
    MODE_CURSOR = 0,
    MODE_SCROLL = 1,
};

// SWIPE direction codes (dominant axis at recognition, 4-way only).
enum SwipeDir : uint8_t {
    SWIPE_UP    = 1,
    SWIPE_DOWN  = 2,
    SWIPE_LEFT  = 3,
    SWIPE_RIGHT = 4,
};

static const uint8_t VERSION = 2;
static const size_t HEADER_SIZE = 32;
static const size_t MAX_PACKET = 37; // header + largest payload (FMOVE)

#pragma pack(push, 1)
struct Header {
    char magic[4];        // "VTPD"
    uint8_t version;      // 2
    uint8_t type;         // Type
    uint16_t flags;       // reserved, 0
    uint32_t seq;         // monotonic per session
    uint32_t timestampMs; // sender uptime ms
    char passcode[16];    // NUL-padded ASCII
};
struct MovePayload {
    int16_t dx;
    int16_t dy;
};
struct FMovePayload {
    uint8_t fingers; // contact count (1..8)
    int16_t dx;
    int16_t dy;
};
struct SwipePayload {
    uint8_t fingers;   // contact count (3 in the v2 gesture set)
    uint8_t direction; // SwipeDir
};
struct ZoomPayload {
    uint8_t fingers; // contact count (2 in the v2 gesture set)
    int16_t dSpread; // spread change, >0 = spreading apart (zoom in)
};
struct EndPayload {
    uint8_t ended_type; // the stream type that just ended (FMOVE / ZOOM)
    uint8_t fingers;    // contact count of that bout
};
struct ButtonPayload {
    uint8_t button; // Button
    uint8_t action; // ButtonAction
};
struct ModePayload {
    uint8_t mode; // Mode
};
struct HelloPayload {
    uint8_t mode;  // Mode
    uint8_t flags; // bit0 = startInScrollMode
};
struct VPadPayload {
    uint8_t button; // VPadButton
    uint8_t action; // ButtonAction (UP/DOWN edge)
};
#pragma pack(pop)

static_assert(sizeof(Header) == 32, "VTP header must be 32 bytes");
static_assert(sizeof(MovePayload) == 4, "MOVE payload must be 4 bytes");
static_assert(sizeof(FMovePayload) == 5, "FMOVE payload must be 5 bytes");
static_assert(sizeof(SwipePayload) == 2, "SWIPE payload must be 2 bytes");
static_assert(sizeof(ZoomPayload) == 3, "ZOOM payload must be 3 bytes");
static_assert(sizeof(EndPayload) == 2, "END payload must be 2 bytes");
static_assert(sizeof(ButtonPayload) == 2, "BUTTON payload must be 2 bytes");
static_assert(sizeof(ModePayload) == 1, "MODE payload must be 1 byte");
static_assert(sizeof(HelloPayload) == 2, "HELLO payload must be 2 bytes");
static_assert(sizeof(VPadPayload) == 2, "VPAD payload must be 2 bytes");

// Fills the common header; returns HEADER_SIZE. Passcode is truncated to
// 16 bytes and NUL-padded.
inline size_t fillHeader(void* out, uint8_t type, uint32_t seq,
                         uint32_t timestampMs, const char* passcode) {
    Header* h = (Header*)out;
    h->magic[0] = 'V';
    h->magic[1] = 'T';
    h->magic[2] = 'P';
    h->magic[3] = 'D';
    h->version = VERSION;
    h->type = type;
    h->flags = 0;
    h->seq = seq;
    h->timestampMs = timestampMs;
    memset(h->passcode, 0, sizeof(h->passcode));
    if (passcode) {
        size_t n = strlen(passcode);
        if (n > sizeof(h->passcode)) n = sizeof(h->passcode);
        memcpy(h->passcode, passcode, n);
    }
    return HEADER_SIZE;
}

inline size_t buildMove(void* out, int16_t dx, int16_t dy, uint32_t seq,
                        uint32_t timestampMs, const char* passcode) {
    fillHeader(out, MOVE, seq, timestampMs, passcode);
    MovePayload* p = (MovePayload*)((uint8_t*)out + HEADER_SIZE);
    p->dx = dx;
    p->dy = dy;
    return HEADER_SIZE + sizeof(MovePayload);
}

inline size_t buildFMove(void* out, uint8_t fingers, int16_t dx, int16_t dy,
                         uint32_t seq, uint32_t timestampMs, const char* passcode) {
    fillHeader(out, FMOVE, seq, timestampMs, passcode);
    FMovePayload* p = (FMovePayload*)((uint8_t*)out + HEADER_SIZE);
    p->fingers = fingers;
    p->dx = dx;
    p->dy = dy;
    return HEADER_SIZE + sizeof(FMovePayload);
}

inline size_t buildSwipe(void* out, uint8_t fingers, uint8_t direction, uint32_t seq,
                         uint32_t timestampMs, const char* passcode) {
    fillHeader(out, SWIPE, seq, timestampMs, passcode);
    SwipePayload* p = (SwipePayload*)((uint8_t*)out + HEADER_SIZE);
    p->fingers = fingers;
    p->direction = direction;
    return HEADER_SIZE + sizeof(SwipePayload);
}

inline size_t buildZoom(void* out, uint8_t fingers, int16_t dSpread, uint32_t seq,
                        uint32_t timestampMs, const char* passcode) {
    fillHeader(out, ZOOM, seq, timestampMs, passcode);
    ZoomPayload* p = (ZoomPayload*)((uint8_t*)out + HEADER_SIZE);
    p->fingers = fingers;
    p->dSpread = dSpread;
    return HEADER_SIZE + sizeof(ZoomPayload);
}

inline size_t buildEnd(void* out, uint8_t ended_type, uint8_t fingers, uint32_t seq,
                       uint32_t timestampMs, const char* passcode) {
    fillHeader(out, END, seq, timestampMs, passcode);
    EndPayload* p = (EndPayload*)((uint8_t*)out + HEADER_SIZE);
    p->ended_type = ended_type;
    p->fingers = fingers;
    return HEADER_SIZE + sizeof(EndPayload);
}

inline size_t buildButton(void* out, uint8_t button, uint8_t action, uint32_t seq,
                           uint32_t timestampMs, const char* passcode) {
    fillHeader(out, BUTTON, seq, timestampMs, passcode);
    ButtonPayload* p = (ButtonPayload*)((uint8_t*)out + HEADER_SIZE);
    p->button = button;
    p->action = action;
    return HEADER_SIZE + sizeof(ButtonPayload);
}

inline size_t buildMode(void* out, uint8_t mode, uint32_t seq,
                         uint32_t timestampMs, const char* passcode) {
    fillHeader(out, MODE, seq, timestampMs, passcode);
    ModePayload* p = (ModePayload*)((uint8_t*)out + HEADER_SIZE);
    p->mode = mode;
    return HEADER_SIZE + sizeof(ModePayload);
}

inline size_t buildHello(void* out, uint8_t mode, bool startInScroll, uint32_t seq,
                          uint32_t timestampMs, const char* passcode) {
    fillHeader(out, HELLO, seq, timestampMs, passcode);
    HelloPayload* p = (HelloPayload*)((uint8_t*)out + HEADER_SIZE);
    p->mode = mode;
    p->flags = startInScroll ? 1 : 0;
    return HEADER_SIZE + sizeof(HelloPayload);
}

inline size_t buildPing(void* out, uint32_t seq, uint32_t timestampMs,
                         const char* passcode) {
    return fillHeader(out, PING, seq, timestampMs, passcode);
}

inline size_t buildVPad(void* out, uint8_t button, uint8_t action, uint32_t seq,
                        uint32_t timestampMs, const char* passcode) {
    fillHeader(out, VPAD, seq, timestampMs, passcode);
    VPadPayload* p = (VPadPayload*)((uint8_t*)out + HEADER_SIZE);
    p->button = button;
    p->action = action;
    return HEADER_SIZE + sizeof(VPadPayload);
}

} // namespace vtp
