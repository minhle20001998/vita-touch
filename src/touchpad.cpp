#include "touchpad.h"
#include "protocol.h"
#include "udp.h"
#include <psp2/kernel/processmgr.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace touchpad {

namespace {

// Event log capacity.
const int LOG_LINES = 8;

// Single-frame teleport guard: legitimate finger motion peaks around ~85
// panel units/frame (measured on hardware across move/scroll sessions); anything
// beyond 120 is panel fusion jitter, absorbed (re-anchor, no emit, no accrual).
const int JUMP_CAP = 120;

enum Phase { IDLE, ONE, TWO, THREE };

struct State {
    Phase phase = IDLE;
    int lastX = 0, lastY = 0;   // centroid at last frame (streaming deltas)
    int ax = 0, ay = 0;         // gesture anchor (centroid at bout start)
    int dist = 0;               // accumulated Manhattan distance
    int count = 0;              // finger count the anchor was taken with
    int emptyFrames = 0;        // consecutive n==0 frames (release debounce)
    int bout = 0;               // two-finger lock: 0 undecided, 1 scroll, 2 zoom
    int twoDX = 0, twoDY = 0;     // TWO-window vector displacement (triple seed)
    float axisX = 1, axisY = 0; // unit inter-finger vector at bout entry
    int pfX0 = 0, pfY0 = 0;     // prev-frame finger positions (proximity matched)
    int pfX1 = 0, pfY1 = 0;
    float radialAcc = 0;        // accumulated radial spread change (the line test)
    int zoomFrames = 0;         // consecutive radially-dominant frames
    float pinchCarry = 0;       // fractional ZOOM remainder across frames
    int pendingCount = -1;      // unconfirmed count (flicker hysteresis)
    int pendingFrames = 0;
    bool mergedTwo = false;     // sustained 2->1 merge: single blob reports for
                                // two close contacts (panel merged them)
    bool zoomLockAllowed = true; // pinch needs a clean 2-landing (see below)
    uint8_t streamType = 0;     // live motion stream to terminate (FMOVE/ZOOM)
    uint8_t streamFingers = 0;  // contact count of that stream
    // Session-level 3-finger candidacy: real hands lift staggered (3->2->1),
    // so tap/swipe intent must survive step-downs until full release.
    bool tripleActive = false;  // a 3+ bout occurred this contact session
    bool triArmed = false;      // its release may tap (clean landing only)
    int triDX = 0, triDY = 0;   // 3-bout displacement (incl. step-down tails)
    int triDist = 0;            // accumulated 3-bout motion (tap gate)
    int triFingers = 0;         // contact count to report
    uint8_t pendingTap = 0;     // 0 none; 2 = right-click on clean release
                                // (2-finger lift stagger: the release outlives
                                // the TWO phase, so the tap must too)
    bool sessionHadButton = false; // any BUTTON DOWN this session: no further
                                // clicks (kills post-drag placement artifacts)
    bool holdMove = false;      // touchdown hold: fresh-bout cursor motion is
                                // buffered, not emitted (see below)
    int holdDX = 0, holdDY = 0; // buffered fresh-bout motion (raw panel units)
    int pvx = 0, pvy = 0;       // previous frame's raw delta (flick continuity)
    uint32_t startMs = 0;
    bool moved = false;         // slop exceeded: motion gate
    bool tapArmed = false;      // release may click (disarmed by any juggling)
    bool dragging = false;      // left button currently held for drag
    bool leftHeld = false;
    float carryX = 0, carryY = 0; // stick fractional remainder across frames
    int fingers = 0;
};

State g;
uint32_t g_seq = 0;
char g_log[LOG_LINES][64];
int g_logHead = 0; // next slot to write
int g_logUsed = 0;
bool g_logOn = true; // mirrors config.showEventLog, refreshed per update

uint32_t nowMs() { return sceKernelGetProcessTimeLow() / 1000; }

void logEv(const char* msg) {
    if (!g_logOn) return;
    snprintf(g_log[g_logHead], sizeof(g_log[g_logHead]), "%s", msg);
    g_logHead = (g_logHead + 1) % LOG_LINES;
    if (g_logUsed < LOG_LINES) g_logUsed++;
}

// Teleport guard with flick continuity: an over-cap delta is treated as real
// motion (allowed through) only when the previous frame was also fast and
// moving roughly the same way; otherwise it is an isolated panel-fusion
// teleport and must be absorbed. Records the raw delta so a sustained fast
// streak stays recognised even across an absorbed frame. Returns true when the
// caller must re-anchor and emit nothing.
bool absorbJump(int dx, int dy) {
    int m = abs(dx) + abs(dy);
    if (m <= JUMP_CAP) {
        g.pvx = dx;
        g.pvy = dy;
        return false;
    }
    int pm = abs(g.pvx) + abs(g.pvy);
    long dot = (long)dx * g.pvx + (long)dy * g.pvy;
    bool flick = pm > JUMP_CAP / 2 && dot > 0 && m <= JUMP_CAP * 5;
    g.pvx = dx;
    g.pvy = dy;
    return !flick;
}

int16_t scaleClamp(float v) {
    int r = (int)(v >= 0 ? v + 0.5f : v - 0.5f);
    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    return (int16_t)r;
}

bool sendButtonEv(AppState& app, uint8_t button, uint8_t action) {
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildButton(buf, button, action, g_seq++, nowMs(),
                                app.config.passcode.c_str());
    return udp::sendRaw(buf, (unsigned)n);
}

bool sendMoveUnits(AppState& app, int16_t dx, int16_t dy) {
    if (dx == 0 && dy == 0) return true;
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildMove(buf, dx, dy, g_seq++, nowMs(),
                              app.config.passcode.c_str());
    return udp::sendRaw(buf, (unsigned)n);
}

bool sendMoveEv(AppState& app, int dx, int dy) {
    return sendMoveUnits(app, scaleClamp(dx * app.config.sensitivity),
                         scaleClamp(dy * app.config.sensitivity));
}

bool sendFMoveUnits(AppState& app, uint8_t fingers, int16_t dx, int16_t dy) {
    if (dx == 0 && dy == 0) return true;
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildFMove(buf, fingers, dx, dy, g_seq++, nowMs(),
                               app.config.passcode.c_str());
    return udp::sendRaw(buf, (unsigned)n);
}

// Multi-finger motion scales with scrollSensitivity (scroll-context knob);
// fingers=1 in scroll mode shares it.
bool sendFMoveEv(AppState& app, uint8_t fingers, int dx, int dy) {
    return sendFMoveUnits(app, fingers, scaleClamp(dx * app.config.scrollSensitivity),
                          scaleClamp(dy * app.config.scrollSensitivity));
}

bool sendSwipeEv(AppState& app, uint8_t fingers, uint8_t direction) {
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildSwipe(buf, fingers, direction, g_seq++, nowMs(),
                               app.config.passcode.c_str());
    return udp::sendRaw(buf, (unsigned)n);
}

bool sendZoomUnits(AppState& app, uint8_t fingers, int16_t dSpread) {
    if (dSpread == 0) return true;
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildZoom(buf, fingers, dSpread, g_seq++, nowMs(),
                              app.config.passcode.c_str());
    return udp::sendRaw(buf, (unsigned)n);
}

bool sendEndEv(AppState& app, uint8_t ended_type, uint8_t fingers) {
    uint8_t buf[vtp::MAX_PACKET];
    size_t n = vtp::buildEnd(buf, ended_type, fingers, g_seq++, nowMs(),
                             app.config.passcode.c_str());
    return udp::sendRaw(buf, (unsigned)n);
}

const char* streamName(uint8_t type) {
    if (type == vtp::FMOVE) return "FMOVE";
    if (type == vtp::ZOOM) return "ZOOM";
    return "?";
}

// Closes the live motion stream, if any: every FMOVE/ZOOM bout the server saw
// gets an explicit terminator (press/release semantics without timeouts).
// Best-effort like all UDP — the server keeps a generous backstop timeout.
void flushStream(AppState& app) {
    if (g.streamType == 0) return;
    sendEndEv(app, g.streamType, g.streamFingers);
    char msg[64];
    snprintf(msg, sizeof(msg), "END %s%d", streamName(g.streamType), g.streamFingers);
    logEv(msg);
    g.streamType = 0;
    g.streamFingers = 0;
}

// Marks the bout as streaming (call when emitting FMOVE/ZOOM motion).
void markStream(uint8_t type, uint8_t fingers) {
    g.streamType = type;
    g.streamFingers = fingers;
}

void leftDown(AppState& app, const char* why) {
    sendButtonEv(app, vtp::BUTTON_LEFT, vtp::ACTION_DOWN);
    g.leftHeld = true;
    g.sessionHadButton = true; // this session may not produce further clicks
    char msg[64];
    snprintf(msg, sizeof(msg), "L down (%s)", why);
    logEv(msg);
}

void leftUp(AppState& app, const char* why) {
    // Retry: a lost UP leaves the PC button stuck down. UDP is cheap.
    for (int i = 0; i < 3; i++) {
        if (sendButtonEv(app, vtp::BUTTON_LEFT, vtp::ACTION_UP)) break;
    }
    g.leftHeld = false;
    char msg[64];
    snprintf(msg, sizeof(msg), "L up (%s)", why);
    logEv(msg);
}

void beginGesture(int cx, int cy, Phase phase, bool suppressTap, int n) {
    g.phase = phase;
    g.lastX = cx;
    g.lastY = cy;
    g.ax = cx;
    g.ay = cy;
    g.dist = 0;
    g.count = n;
    g.bout = 0;
    g.pendingCount = -1; // consumed (confirmed) or fresh; never stale past entry
    g.pendingFrames = 0;
    g.streamType = 0; // fresh bout; the old stream was flushed by the caller
    g.streamFingers = 0;
    g.pvx = 0; // fresh bout: no motion streak carried in
    g.pvy = 0;
    g.startMs = nowMs();
    g.moved = false;
    g.tapArmed = !suppressTap; // any contact juggling disarms the release-click
    g.pendingTap = 0; // fresh bout; step-down sites re-arm below if warranted
    g.mergedTwo = false; // entries always start unmerged
    g.holdMove = false; // entries never inherit the touchdown hold...
    g.holdDX = 0;       // ...except IDLE->ONE below, which arms it fresh
    g.holdDY = 0;
    g.dragging = false;
}

// (Re)starts session-level 3-finger tracking. Call on every THREE entry.
void setupTriple(bool armed, int n) {
    g.tripleActive = true;
    g.triArmed = armed;
    g.triDX = 0;
    g.triDY = 0;
    g.triDist = 0;
    g.triFingers = n;
    g.pendingTap = 0; // the triple owns the release from here on
}

void clearTriple() {
    g.tripleActive = false;
    g.triArmed = false;
    g.triDX = 0;
    g.triDY = 0;
    g.triDist = 0;
    g.triFingers = 0;
}

uint8_t swipeDir(int dx, int dy) {
    if (abs(dx) >= abs(dy)) return dx > 0 ? vtp::SWIPE_RIGHT : vtp::SWIPE_LEFT;
    return dy > 0 ? vtp::SWIPE_DOWN : vtp::SWIPE_UP;
}

const char* swipeName(uint8_t dir) {
    switch (dir) {
        case vtp::SWIPE_UP: return "up";
        case vtp::SWIPE_DOWN: return "down";
        case vtp::SWIPE_LEFT: return "left";
        case vtp::SWIPE_RIGHT: return "right";
        default: return "?";
    }
}

// Fires the inherited 3-finger release (tap or swipe) after staggered lifts.
// Returns true if it consumed the release.
bool tripleRelease(AppState& app) {
    if (!g.tripleActive) return false;
    if (g.leftHeld) leftUp(app, "triple end"); // defensive; normally false
    flushStream(app); // close any live stream before the discrete event
    if (g.triArmed && g.triDist <= app.config.tapSlop) {
        sendButtonEv(app, vtp::BUTTON_MIDDLE, vtp::ACTION_DOWN);
        sendButtonEv(app, vtp::BUTTON_MIDDLE, vtp::ACTION_UP);
        logEv("3-f tap -> M click");
    } else {
        int m = abs(g.triDX) + abs(g.triDY);
        if (m >= app.config.swipeDist) {
            uint8_t dir = swipeDir(g.triDX, g.triDY);
            sendSwipeEv(app, (uint8_t)g.triFingers, dir);
            char msg[64];
            snprintf(msg, sizeof(msg), "3-f swipe %s", swipeName(dir));
            logEv(msg);
        }
    }
    clearTriple();
    g.phase = IDLE;
    return true;
}

} // namespace

void reset() {
    g = State();
    g_seq = 0;
}

// Processes one frame of a merged pair: the panel reports a single blob for
// two close contacts. The bout stays TWO (packets keep fingers=2); motion
// streams FMOVE(2) once scroll-locked, zoom evaluation is frozen (spread is
// unmeasurable from one blob) and resumes on split-back.
void twoLite(AppState& app, int cx, int cy) {
    int dx = cx - g.lastX;
    int dy = cy - g.lastY;
    g.lastX = cx;
    g.lastY = cy;
    if (absorbJump(dx, dy)) {
        logEv("jump absorbed");
        return;
    }
    if (g.tripleActive) { // step-down tail still belongs to the 3-bout
        g.triDX += dx;
        g.triDY += dy;
        g.triDist += abs(dx) + abs(dy);
    }
    if (g.bout == 0) {
        int cDisp = abs(cx - g.ax) + abs(cy - g.ay);
        if (cDisp > app.config.tapSlop && g.zoomFrames == 0) {
            g.bout = 1;
            g.moved = true;
        }
    }
    if (g.bout == 1 && (dx != 0 || dy != 0)) {
        sendFMoveEv(app, 2, dx, dy);
        markStream(vtp::FMOVE, 2);
        char msg[64];
        snprintf(msg, sizeof(msg), "2-f FMOVE %+d %+d", dx, dy);
        logEv(msg);
    }
}

void suspend(AppState& app) {
    if (g.leftHeld) leftUp(app, "leave screen");
    flushStream(app); // terminate any live motion stream first
    clearTriple(); // abandon any inherited 3-finger release
    g.sessionHadButton = false;
    g.pendingTap = 0;
    g.mergedTwo = false;
    g.holdMove = false;
    g.holdDX = 0;
    g.holdDY = 0;
    g.pendingCount = -1;
    g.pendingFrames = 0;
    g.moved = false;
    g.dist = 0;
    g.phase = IDLE;
    g.dragging = false;
    g.tapArmed = false;
    g.emptyFrames = 0;
    g.pvx = 0;
    g.pvy = 0;
    g.fingers = 0;
}

// Completes the pending bout immediately: tap/click/swipe evaluation,
// session resets, stream termination. Called on debounced release and on
// re-touch at a new spot (fast re-tap must not merge into the old bout).
void doRelease(AppState& app) {
    // A 3-finger bout anywhere in this contact session owns the release:
    // staggered lifts (3->2->1) must not lose the tap/swipe.
    if (g.tripleActive) {
        tripleRelease(app);
    } else if (g.phase == ONE) {
        if (g.dragging) {
            leftUp(app, "drag end");
            g.dragging = false;
        } else if (!g.moved && (g.tapArmed || g.pendingTap == 2)) {
            if (g.pendingTap == 2) {
                sendButtonEv(app, vtp::BUTTON_RIGHT, vtp::ACTION_DOWN);
                sendButtonEv(app, vtp::BUTTON_RIGHT, vtp::ACTION_UP);
                logEv("staggered 2-tap -> R click");
            } else {
                sendButtonEv(app, vtp::BUTTON_LEFT, vtp::ACTION_DOWN);
                sendButtonEv(app, vtp::BUTTON_LEFT, vtp::ACTION_UP);
                logEv("tap -> L click");
            }
        }
        g.phase = IDLE;
    } else if (g.phase == TWO) {
        if (g.tapArmed && !g.moved) {
            sendButtonEv(app, vtp::BUTTON_RIGHT, vtp::ACTION_DOWN);
            sendButtonEv(app, vtp::BUTTON_RIGHT, vtp::ACTION_UP);
            logEv("2-f tap -> R click");
        }
        g.phase = IDLE;
    }
    // Session ends here: stale motion/button state must not leak into the
    // next session's landing honesty checks.
    g.moved = false;
    g.dist = 0;
    g.sessionHadButton = false;
    g.pendingTap = 0;
    g.holdMove = false;
    g.holdDX = 0;
    g.holdDY = 0;
    g.emptyFrames = 0;
    flushStream(app); // every ended bout terminates its live stream, if any
}

void update(AppState& app) {
    g_logOn = app.config.showEventLog;
    if (app.input.touchRet < 0) return; // keep state across a failed peek
    unsigned n = app.input.touch.reportNum;
    if (n > 8) n = 8;
    g.fingers = (int)n;
    uint32_t now = nowMs();

    if (n == 0) {
        // Release debounce: a single dropped frame must not end the gesture
        // (it would cash out a motionless hold as a click, or split a drag).
        if (++g.emptyFrames < 3) return;
        doRelease(app);
        return;
    }
    // A contact reappearing inside the release debounce window is either panel
    // flicker (same spot: resume the bout, no release) or a fast re-tap
    // (new spot: complete the old bout NOW, then start fresh below).
    // Without the split, tap-A-lift-tap-B merges and the cursor teleports A->B.
    int pendingEmpties = g.emptyFrames;
    g.emptyFrames = 0;

    // Centroid of active fingers unifies one- and two-finger motion.
    int cx = 0, cy = 0;
    for (unsigned i = 0; i < n; i++) {
        cx += app.input.touch.report[i].x;
        cy += app.input.touch.report[i].y;
    }
    cx /= (int)n;
    cy /= (int)n;

    if (pendingEmpties > 0) {
        if (abs(cx - g.lastX) + abs(cy - g.lastY) <= app.config.tapSlop) {
            // Flicker: resume the bout silently (anchor keeps the old spot so
            // the gap frames contribute no phantom motion).
        } else if (g.phase != IDLE) {
            doRelease(app); // old bout ends here; fresh entry below
        } else {
            clearTriple(); // defensive; IDLE carries no bout
        }
    }

    // Count-change confirmation (merge/split hysteresis), asymmetric: adding
    // contacts mid-bout (ghosts, palm grazes, hovering fingers) needs 4
    // consecutive frames (~65 ms) — deliberate additions always outlast that.
    // Removing contacts needs only 2 (lifts are fast and intentional). Fresh
    // touches from IDLE stay instant; release keeps its own 3-frame debounce.
    // A sustained merge is handled separately (mergedTwo), not here.
    if (!g.mergedTwo && g.phase != IDLE && (int)n != g.count) {
        int need = ((int)n > g.count) ? 4 : 2;
        if (g.pendingCount != (int)n) {
            g.pendingCount = (int)n;
            g.pendingFrames = 1;
        } else {
            g.pendingFrames++;
        }
        if (g.pendingFrames < need) return; // unconfirmed: hold state, emit nothing
        g.pendingCount = -1;
        g.pendingFrames = 0;
        // fall through: entries below adopt the new count via beginGesture
    } else {
        g.pendingCount = -1;
        g.pendingFrames = 0;
    }

    if (n == 1) {
        // For one finger the centroid is the finger position.
        if (g.phase == IDLE) {
            flushStream(app); // no-op on a fresh down; keeps every entry safe
            clearTriple(); // fresh contact session
            g.sessionHadButton = false;
            beginGesture(cx, cy, ONE, false, 1);
            // Touchdown hold (cursor mode only): fresh-bout motion accumulates
            // silently for ~landingHoldMs. A count-up inside the window discards
            // it (close landing -> clean switch, zero cursor jump); otherwise it
            // flushes as one catch-up MOVE. Taps/drags unaffected (tap honesty
            // runs on dist, drag arms far outside the window).
            g.holdMove = app.config.landingHoldMs > 0 && !app.scrollMode;
        } else if (g.phase == TWO) {
            if (g.mergedTwo) {
                twoLite(app, cx, cy); // continuing merge, not a step-down
            } else {
                // First confirmed 1-frame: merged blob or genuine lift?
                // A merged blob sits near the old centroid; a lifted finger
                // leaves the remainder near one old contact (~spacing/2 away,
                // always above spacing/3) — so this threshold cannot misfire.
                int spacing = abs(g.pfX0 - g.pfX1) + abs(g.pfY0 - g.pfY1);
                int thresh = spacing / 3;
                if (thresh > 25) thresh = 25;
                int dC = abs(cx - g.lastX) + abs(cy - g.lastY);
                if (dC <= thresh) {
                    g.mergedTwo = true;
                    logEv("merged pair");
                    twoLite(app, cx, cy);
                } else {
                    // Genuine lift: the remaining finger starts fresh with full
                    // slop but must not tap (lifting it ends the scroll, it must
                    // not click) — unless it is the tail of a clean 2-tap
                    // landing staggered on lift, which inherits a right-click.
                    bool wasClean = g.tapArmed && !g.moved;
                    flushStream(app); // terminate the old bout's stream, if any
                    beginGesture(cx, cy, ONE, true, 1);
                    if (!g.tripleActive && wasClean) g.pendingTap = 2;
                }
            }
        } else if (g.phase == THREE) {
            // 3->1 step-down: same treatment, fresh anchor, must not click.
            flushStream(app);
            beginGesture(cx, cy, ONE, true, 1);
        } else {
            int dx = cx - g.lastX;
            int dy = cy - g.lastY;
            if (absorbJump(dx, dy)) {
                // Teleport guard: re-anchor, emit and accrue nothing. Isolated
                // fusion-jitter spikes are absorbed; a sustained fast flick
                // (consecutive same-direction over-cap frames) passes through.
                g.lastX = cx;
                g.lastY = cy;
                logEv("jump absorbed");
            } else {
                g.lastX = cx;
                g.lastY = cy;
                g.dist += abs(dx) + abs(dy);
                if (g.tripleActive) { // step-down tail still belongs to the 3-bout
                    g.triDX += dx;
                    g.triDY += dy;
                    g.triDist += abs(dx) + abs(dy);
                }
                uint32_t heldFor = now - g.startMs;
                uint32_t holdMs = (uint32_t)app.config.holdMs;
                if (!g.dragging && !g.moved && heldFor >= holdMs) {
                    if ((dx != 0 || dy != 0) && g.dist > app.config.tapSlop) {
                        // First motion after a matured hold: deferred drag arm.
                        // Nothing was sent during the motionless hold, so slow
                        // multi-finger landings stay clean. DOWN and first MOVE
                        // go out together — identical on the wire to the old
                        // fire-at-hold behavior from the server's view.
                        g.pendingTap = 0;
                        if (!app.scrollMode) leftDown(app, "hold");
                        g.dragging = true;
                        g.moved = true;
                    } else if (heldFor >= holdMs * 3U) {
                        // Failsafe: a very long motionless hold still presses, so
                        // long-press mappings keep working (3x the normal hold).
                        g.pendingTap = 0;
                        if (!app.scrollMode) leftDown(app, "hold-long");
                        g.dragging = true;
                        g.moved = true;
                    }
                } else if (!g.moved && g.dist > app.config.tapSlop) {
                    g.moved = true;
                    g.pendingTap = 0;
                }
                if (g.moved && (dx != 0 || dy != 0)) {
                    char msg[64];
                    if (app.scrollMode) {
                        sendFMoveEv(app, 1, dx, dy);
                        markStream(vtp::FMOVE, 1);
                        snprintf(msg, sizeof(msg), "FMOVE1 %+d %+d", dx, dy);
                    } else if (g.holdMove && now - g.startMs < (uint32_t)app.config.landingHoldMs) {
                        // Touchdown hold: accumulate silently. A count-up inside
                        // the window discards the buffer (close landing -> clean
                        // switch, zero cursor jump); a lift evaluates the tap off
                        // dist; expiry flushes below.
                        g.holdDX += dx;
                        g.holdDY += dy;
                    } else {
                        if (g.holdMove) {
                            // Window expired: flush the buffered motion first so
                            // no travel is lost, then go live.
                            g.holdMove = false;
                            if (g.holdDX != 0 || g.holdDY != 0) {
                                sendMoveEv(app, g.holdDX, g.holdDY);
                                char fmsg[64];
                                snprintf(fmsg, sizeof(fmsg), "MOVE flush %+d %+d",
                                         g.holdDX, g.holdDY);
                                logEv(fmsg);
                            }
                            g.holdDX = 0;
                            g.holdDY = 0;
                        }
                        sendMoveEv(app, dx, dy);
                        snprintf(msg, sizeof(msg), "%sMOVE %+d %+d",
                                 g.dragging ? "drag " : "", dx, dy);
                        logEv(msg);
                    }
                }
            }
        }
    } else if (n == 2) {
        // Two fingers: FMOVE scroll stream, ZOOM pinch stream, or right-click.
        // The pinch test is radial: per-finger motion is projected onto the
        // inter-finger axis fixed at bout entry, so only spreading-apart /
        // squeezing-together along the connecting line counts as zoom.
        // Parallel drifting scores ~zero no matter the direction (diagonal
        // pinches pass; diagonal scrolls never zoom). Zoom additionally needs
        // 3 consecutive radially-dominant frames (settling blips can't lock);
        // scroll keeps its single-crossing lock and wins exact ties.
        const SceTouchReport& r0 = app.input.touch.report[0];
        const SceTouchReport& r1 = app.input.touch.report[1];
        if (g.phase != TWO) {
            bool fromThree = (g.phase == THREE);
            // Fresh two-finger contact (from IDLE, or aborting a one-finger
            // gesture when the second finger lands).
            if (g.phase == ONE && g.dragging) {
                leftUp(app, "2nd finger");
                g.dragging = false;
            }
            // From THREE (3->2 step-down) the fresh contact must not click.
            if (g.phase == IDLE) {
                clearTriple(); // fresh session
                g.sessionHadButton = false;
            }
            flushStream(app); // terminate the old bout's stream, if any
            // No clicks after any button-press this session: a drag-aborted
            // landing must stay silent on release.
            beginGesture(cx, cy, TWO, fromThree || g.sessionHadButton, 2);
            // Pinch needs a clean two-finger landing: after a 3->2 step-down
            // the remaining pair is usually still spreading from the swipe
            // motion, which must not lock zoom (scroll streaming is unaffected).
            g.zoomLockAllowed = !fromThree;
            g.twoDX = 0;
            g.twoDY = 0;
            g.pfX0 = r0.x; g.pfY0 = r0.y;
            g.pfX1 = r1.x; g.pfY1 = r1.y;
            float vx = (float)(r1.x - r0.x);
            float vy = (float)(r1.y - r0.y);
            float vlen = sqrtf(vx * vx + vy * vy);
            if (vlen < 1.0f) { vx = 1.0f; vy = 0.0f; vlen = 1.0f; }
            g.axisX = vx / vlen;
            g.axisY = vy / vlen;
            g.radialAcc = 0;
            g.zoomFrames = 0;
            g.pinchCarry = 0;
        } else {
            if (g.mergedTwo) {
                // Split back to two measurable contacts: resume full tracking.
                // Centroid is continuous (blob ≈ old centroid), so no re-anchor
                // and no jump; just refresh per-finger tracking.
                g.mergedTwo = false;
                g.pfX0 = r0.x; g.pfY0 = r0.y;
                g.pfX1 = r1.x; g.pfY1 = r1.y;
            }
            int dx = cx - g.lastX;
            int dy = cy - g.lastY;
            if (absorbJump(dx, dy)) {
                // Teleport guard: re-anchor everything, emit and accrue nothing.
                g.lastX = cx;
                g.lastY = cy;
                g.pfX0 = r0.x; g.pfY0 = r0.y;
                g.pfX1 = r1.x; g.pfY1 = r1.y;
                logEv("jump absorbed");
            } else {
            // Match current reports to prev positions by proximity (panel may
            // reorder report slots between frames).
            int d00 = abs(r0.x - g.pfX0) + abs(r0.y - g.pfY0);
            int d01 = abs(r0.x - g.pfX1) + abs(r0.y - g.pfY1);
            int d10 = abs(r1.x - g.pfX0) + abs(r1.y - g.pfY0);
            int d11 = abs(r1.x - g.pfX1) + abs(r1.y - g.pfY1);
            int a0x, a0y, a1x, a1y; // prev positions aligned to r0, r1
            if (d00 + d11 <= d01 + d10) {
                a0x = g.pfX0; a0y = g.pfY0; a1x = g.pfX1; a1y = g.pfY1;
            } else {
                a0x = g.pfX1; a0y = g.pfY1; a1x = g.pfX0; a1y = g.pfY0;
            }
            float rad0 = (r0.x - a0x) * g.axisX + (r0.y - a0y) * g.axisY;
            float rad1 = (r1.x - a1x) * g.axisX + (r1.y - a1y) * g.axisY;
            float rD = rad1 - rad0; // >0 spreading apart, <0 squeezing
            int cD = abs(dx) + abs(dy);
            g.lastX = cx;
            g.lastY = cy;
            g.pfX0 = r0.x; g.pfY0 = r0.y;
            g.pfX1 = r1.x; g.pfY1 = r1.y;
            if (g.tripleActive) { // step-down tail still belongs to the 3-bout
                g.triDX += dx;
                g.triDY += dy;
                g.triDist += abs(dx) + abs(dy);
            }
            g.twoDX += dx; // TWO-window vector travel (triple seed on 2->3)
            g.twoDY += dy;
            if (g.bout == 0) {
                g.radialAcc += rD;
                if (fabsf(rD) > (float)cD) g.zoomFrames++;
                else g.zoomFrames = 0;
                int cDisp = abs(cx - g.ax) + abs(cy - g.ay);
                if (cDisp > app.config.tapSlop && g.zoomFrames == 0) {
                    g.bout = 1;
                    g.moved = true;
                } else if (g.zoomLockAllowed && fabsf(g.radialAcc) > (float)app.config.pinchSlop && g.zoomFrames >= 3) {
                    g.bout = 2;
                    g.moved = true;
                }
            }
            if (g.bout == 1 && (dx != 0 || dy != 0)) {
                sendFMoveEv(app, 2, dx, dy);
                markStream(vtp::FMOVE, 2);
                char msg[64];
                snprintf(msg, sizeof(msg), "2-f FMOVE %+d %+d", dx, dy);
                logEv(msg);
            } else if (g.bout == 2) {
                g.pinchCarry += rD;
                int dsp = (int)(g.pinchCarry >= 0 ? g.pinchCarry + 0.5f : g.pinchCarry - 0.5f);
                if (dsp != 0) {
                    sendZoomUnits(app, 2, scaleClamp((float)dsp));
                    markStream(vtp::ZOOM, 2);
                    g.pinchCarry -= dsp;
                    char msg[64];
                    snprintf(msg, sizeof(msg), "2-f ZOOM %+d", dsp);
                    logEv(msg);
                }
            }
            } // end non-jump path
        }
    } else {
        // Three+ fingers: FMOVE stream, discrete swipe on release, middle tap.
        if (g.phase != THREE) {
            if (g.phase == ONE && g.dragging) {
                leftUp(app, "3rd finger");
                g.dragging = false;
            }
            // Tap honesty: a 3-landing is clean (may tap) only if the session so
            // far is motionless and press-free. Slow placements qualify; a
            // drag-aborted or scrolled session never taps. No clocks involved.
            // Flicker dropout (3->2->3 with an undecided middle) resumes the
            // session instead of restarting it: displacement and arming carry
            // over, so a momentary contact loss can neither eat the swipe nor
            // mint a phantom tap.
            bool transientGap = g.tripleActive && !g.moved;
            bool cleanLanding = !g.moved && !g.sessionHadButton;
            // A still-undecided TWO bout contributes its real traveled motion
            // (vector-accumulated, so count-change centroid jumps don't count)
            // to the triple vote, so an already-moving swipe doesn't lose its
            // first stretch when the third finger lands late. A locked (moved)
            // bout is a genuine mode switch: fresh accumulators.
            int seedX = 0, seedY = 0;
            if (g.phase == TWO && g.bout == 0) {
                seedX = g.twoDX;
                seedY = g.twoDY;
            }
            flushStream(app); // terminate the old bout's stream, if any
            beginGesture(cx, cy, THREE, !cleanLanding, (int)n);
            if (transientGap) {
                // Keep prior arming and session displacement; just refresh the
                // reported count. The anchor was already re-taken above, so no
                // cursor jump is emitted for the gap.
                g.triFingers = (int)n;
            } else {
                setupTriple(cleanLanding, (int)n);
                g.triDX = seedX;
                g.triDY = seedY;
                g.triDist = abs(seedX) + abs(seedY);
            }
        } else if ((int)n != g.count) {
            // Count change mid-bout (3<->4, grazed palm): re-anchor WITHOUT
            // emitting and disarm. Juggling never clicks or swipes.
            flushStream(app); // terminate the old count's stream, if any
            beginGesture(cx, cy, THREE, true, (int)n);
            setupTriple(false, (int)n); // restart session displacement too
            logEv("re-anchor");
        } else {
            int dx = cx - g.lastX;
            int dy = cy - g.lastY;
            if (absorbJump(dx, dy)) {
                g.lastX = cx;
                g.lastY = cy;
                logEv("jump absorbed");
            } else {
            g.lastX = cx;
            g.lastY = cy;
            g.dist += abs(dx) + abs(dy);
            if (g.tripleActive) { // 3-bout session displacement (swipe rule)
                g.triDX += dx;
                g.triDY += dy;
                g.triDist += abs(dx) + abs(dy);
            }
            if (!g.moved && g.dist > app.config.tapSlop) g.moved = true;
            if (g.moved && (dx != 0 || dy != 0)) {
                sendFMoveEv(app, (uint8_t)g.count, dx, dy);
                markStream(vtp::FMOVE, (uint8_t)g.count);
                char msg[64];
                snprintf(msg, sizeof(msg), "3-f FMOVE %+d %+d", dx, dy);
                logEv(msg);
            }
            } // end non-jump path
        }
    }
}

void updateStick(AppState& app) {
    g_logOn = app.config.showEventLog;
    // Right stick: tilt direction, constant speed (stickSpeed units/sec).
    // Vita sticks rest at ~128 with 0-255 travel; deadzone swallows drift.
    int dx = (int)app.input.pad.rx - 128;
    int dy = (int)app.input.pad.ry - 128;
    const int DEAD = 24;
    long mag2 = (long)dx * dx + (long)dy * dy;
    if (mag2 < (long)DEAD * DEAD) {
        g.carryX = 0;
        g.carryY = 0;
        return;
    }
    float len = sqrtf((float)mag2);
    float step = (float)app.config.stickSpeed / 60.0f;
    g.carryX += dx / len * step;
    g.carryY += dy / len * step;
    int ix = (int)(g.carryX >= 0 ? g.carryX + 0.5f : g.carryX - 0.5f);
    int iy = (int)(g.carryY >= 0 ? g.carryY + 0.5f : g.carryY - 0.5f);
    if (ix == 0 && iy == 0) return;
    g.carryX -= ix;
    g.carryY -= iy;
    int16_t cx = ix > 32767 ? 32767 : (ix < -32768 ? -32768 : (int16_t)ix);
    int16_t cy = iy > 32767 ? 32767 : (iy < -32768 ? -32768 : (int16_t)iy);
    // The stick follows the touchpad mode: cursor drive normally, continuous
    // scroll drive in scroll mode (tilt-and-hold replaces finger dragging).
    char msg[64];
    if (app.scrollMode) {
        sendFMoveUnits(app, 1, cx, cy);
        snprintf(msg, sizeof(msg), "STICK FMOVE1 %+d %+d", ix, iy);
    } else {
        sendMoveUnits(app, cx, cy);
        snprintf(msg, sizeof(msg), "STICK %+d %+d", ix, iy);
    }
    logEv(msg);
}

const char* phaseName() {
    switch (g.phase) {
        case ONE: return "ONE";
        case TWO: return "TWO";
        case THREE: return "THREE";
        default: return "IDLE";
    }
}

int fingerCount() { return g.fingers; }

int phaseId() { return (int)g.phase; } // IDLE=0 ONE=1 TWO=2 THREE=3

int logCount() { return g_logUsed; }

const char* logLine(int i) {
    if (i < 0 || i >= g_logUsed) return "";
    int idx = (g_logHead - 1 - i + LOG_LINES * 2) % LOG_LINES;
    return g_log[idx];
}

} // namespace touchpad
