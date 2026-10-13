// Host test: drives touchpad::update/updateStick with scripted touch frames +
// fake time, dumps captured UDP datagrams for the Python checker.
#include <stdio.h>
#include <string.h>
#include "touchpad.h"

extern uint64_t g_fakeUs;
void cap_reset();
size_t cap_count();
const std::string& cap_get(size_t i);

static AppState app;

static void setTimeMs(uint32_t ms) { g_fakeUs = (uint64_t)ms * 1000; }

// fingers: array of (x, y); n==0 releases all
static void frame(uint32_t ms, int n, const int xy[][2]) {
    setTimeMs(ms);
    memset(&app.input.touch, 0, sizeof(app.input.touch));
    app.input.touchRet = 0;
    app.input.touch.reportNum = n;
    for (int i = 0; i < n; i++) {
        app.input.touch.report[i].id = (uint8_t)(i + 1);
        app.input.touch.report[i].x = (int16_t)xy[i][0];
        app.input.touch.report[i].y = (int16_t)xy[i][1];
        app.input.touch.report[i].force = 50;
    }
    touchpad::update(app);
}

// Release takes 3 consecutive empty frames (release debounce).
static void release(uint32_t ms) {
    frame(ms, 0, NULL);
    frame(ms + 16, 0, NULL);
    frame(ms + 32, 0, NULL);
}

static void dumpScenario(const char* name) {
    printf("SCEN %s %zu\n", name, cap_count());
    for (size_t i = 0; i < cap_count(); i++) printf("PKT %s\n", cap_get(i).c_str());
    cap_reset();
}

int main() {
    memset(&app, 0, sizeof(app));
    app.config.passcode = "pw";
    app.config.sensitivity = 1.0f;
    app.config.scrollSensitivity = 1.0f;
    app.config.tapSlop = 12; // mirror Config() defaults (memset zeroed them)
    app.config.holdMs = 250;
    app.config.stickSpeed = 600;
    app.config.showEventLog = true;
    app.config.swipeDist = 100;
    app.config.pinchSlop = 40;
    app.config.landingHoldMs = 80;

    // 1. plain move: first 80ms buffered (touchdown hold), flushed exact on
    // expiry, then live. Total motion preserved bit-for-bit.
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(0, 1, a); }
    { const int a[][2] = {{110, 100}}; frame(16, 1, a); }
    { const int a[][2] = {{130, 110}}; frame(32, 1, a); }
    { const int a[][2] = {{150, 130}}; frame(96, 1, a); }
    { const int a[][2] = {{170, 150}}; frame(112, 1, a); }
    release(144);
    dumpScenario("move");

    // 2. tap -> left click, no MOVE
    touchpad::reset();
    { const int a[][2] = {{200, 200}}; frame(100, 1, a); }
    release(150);
    dumpScenario("tap");

    // 3. tap-hold-drag
    touchpad::reset();
    { const int a[][2] = {{300, 300}}; frame(200, 1, a); }
    { const int a[][2] = {{300, 300}}; frame(500, 1, a); }
    { const int a[][2] = {{320, 300}}; frame(516, 1, a); }
    release(532);
    dumpScenario("drag");

    // 4. two-finger scroll (no buttons)
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(600, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(616, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(632, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(648, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(664, 2, a); }
    { const int a[][2] = {{100, 440}, {140, 440}}; frame(680, 2, a); }
    release(712);
    dumpScenario("scroll2");

    // 5. two-finger tap -> right click
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(700, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(716, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(732, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(748, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(764, 2, a); }
    release(780);
    dumpScenario("tap2");

    // 6. motionless hold then two-finger scroll (two-stage hold stays silent)
    touchpad::reset();
    { const int a[][2] = {{500, 500}}; frame(800, 1, a); }
    { const int a[][2] = {{500, 500}}; frame(1100, 1, a); }
    { const int a[][2] = {{500, 500}, {540, 500}}; frame(1116, 2, a); }
    { const int a[][2] = {{500, 500}, {540, 500}}; frame(1132, 2, a); }
    { const int a[][2] = {{500, 500}, {540, 500}}; frame(1148, 2, a); }
    { const int a[][2] = {{500, 500}, {540, 500}}; frame(1164, 2, a); }
    { const int a[][2] = {{510, 500}, {550, 500}}; frame(1180, 2, a); }
    { const int a[][2] = {{530, 500}, {570, 500}}; frame(1196, 2, a); }
    release(1228);
    dumpScenario("abort");

    // 7. scroll then lift one finger then release -> no left click
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(1200, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(1216, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(1232, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(1248, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(1264, 2, a); }
    { const int a[][2] = {{130, 400}, {170, 400}}; frame(1280, 2, a); }
    { const int a[][2] = {{130, 400}}; frame(1296, 1, a); }
    { const int a[][2] = {{130, 400}}; frame(1312, 1, a); }
    release(1344);
    dumpScenario("lift21");

    // 8. scroll mode: one-finger motion scrolls, tap still clicks
    touchpad::reset();
    app.scrollMode = true;
    { const int a[][2] = {{100, 100}}; frame(1300, 1, a); }
    { const int a[][2] = {{115, 100}}; frame(1316, 1, a); }
    release(1332);
    { const int a[][2] = {{200, 200}}; frame(1400, 1, a); }
    release(1430);
    app.scrollMode = false;
    dumpScenario("scrollmode");

    // 9. wobble after 2->1: sub-slop trembles emit nothing, no click
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(2000, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2016, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2032, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2048, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2064, 2, a); }
    { const int a[][2] = {{130, 400}, {170, 400}}; frame(2080, 2, a); }
    { const int a[][2] = {{130, 400}}; frame(2096, 1, a); }
    { const int a[][2] = {{130, 400}}; frame(2112, 1, a); }
    { const int a[][2] = {{133, 400}}; frame(2128, 1, a); }
    { const int a[][2] = {{136, 400}}; frame(2144, 1, a); }
    release(2176);
    dumpScenario("wobble");

    // 10. real move after 2->1 streams MOVE, still no click
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(2200, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2216, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2232, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2248, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2264, 2, a); }
    { const int a[][2] = {{130, 400}, {170, 400}}; frame(2280, 2, a); }
    { const int a[][2] = {{130, 400}}; frame(2296, 1, a); }
    { const int a[][2] = {{130, 400}}; frame(2312, 1, a); }
    { const int a[][2] = {{150, 400}}; frame(2328, 1, a); }
    release(2360);
    dumpScenario("lifemove");

    // 11. single dropped frame mid-tap does not split the tap
    touchpad::reset();
    { const int a[][2] = {{300, 300}}; frame(2400, 1, a); }
    frame(2416, 0, NULL);
    { const int a[][2] = {{300, 300}}; frame(2432, 1, a); }
    release(2448);
    dumpScenario("flicker");

    // 12. 2->3 join mid-scroll: FMOVE(2) then FMOVE(3), no click on release
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(2500, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2516, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2532, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2548, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2564, 2, a); }
    { const int a[][2] = {{130, 400}, {170, 400}}; frame(2580, 2, a); }
    { const int a[][2] = {{130, 400}, {170, 400}, {170, 440}}; frame(2596, 3, a); }
    { const int a[][2] = {{130, 400}, {170, 400}, {170, 440}}; frame(2612, 3, a); }
    { const int a[][2] = {{130, 400}, {170, 400}, {170, 440}}; frame(2628, 3, a); }
    { const int a[][2] = {{130, 400}, {170, 400}, {170, 440}}; frame(2644, 3, a); }
    { const int a[][2] = {{150, 400}, {190, 400}, {190, 440}}; frame(2660, 3, a); }
    { const int a[][2] = {{150, 400}, {190, 400}}; frame(2676, 2, a); }
    { const int a[][2] = {{150, 400}, {190, 400}}; frame(2692, 2, a); }
    release(2724);
    dumpScenario("threejoin");

    // 13. quick 1-2-3 cascade + fast release = deliberate 3-tap -> middle click
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(2700, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2716, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2732, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2748, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(2764, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(2780, 3, a); }
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(2796, 3, a); }
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(2812, 3, a); }
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(2828, 3, a); }
    release(2844);
    dumpScenario("cascadetap");

    // 14. right stick full-right for 60 frames at 600 u/s -> (600,0)
    touchpad::reset();
    app.input.pad.rx = 255;
    app.input.pad.ry = 128;
    for (int i = 0; i < 60; i++) {
        setTimeMs(3000 + i * 16);
        touchpad::updateStick(app);
    }
    dumpScenario("stick");

    // 15. stick inside deadzone emits nothing
    touchpad::reset();
    app.input.pad.rx = 140; // dx=12 < 24
    app.input.pad.ry = 128;
    for (int i = 0; i < 10; i++) {
        setTimeMs(4000 + i * 16);
        touchpad::updateStick(app);
    }
    dumpScenario("stickdead");

    // 16. stick diagonal: normalized constant speed
    touchpad::reset();
    app.scrollMode = false;
    app.input.pad.rx = 255;
    app.input.pad.ry = 255;
    for (int i = 0; i < 60; i++) {
        setTimeMs(4100 + i * 16);
        touchpad::updateStick(app);
    }
    dumpScenario("stickdiag");

    // 17. stick in scroll mode emits FMOVE(1), never MOVE
    touchpad::reset();
    app.scrollMode = true;
    app.input.pad.rx = 255;
    app.input.pad.ry = 128;
    for (int i = 0; i < 60; i++) {
        setTimeMs(4200 + i * 16);
        touchpad::updateStick(app);
    }
    app.scrollMode = false;
    dumpScenario("stickscroll");

    // 18. clean 3-finger tap -> middle click
    touchpad::reset();
    { const int a[][2] = {{200, 300}, {240, 300}, {280, 300}}; frame(4300, 3, a); }
    release(4332);
    dumpScenario("threetap");

    // 19. slow placement still taps: 1-2-3 with wide gaps, quick lift.
    // Two-stage hold sends nothing motionless, so the session stays honest.
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(4400, 1, a); }
    { const int a[][2] = {{100, 400}}; frame(4600, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(4800, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(4816, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(4832, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(4848, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(5100, 3, a); }
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(5116, 3, a); }
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(5132, 3, a); }
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(5148, 3, a); }
    release(5180);
    dumpScenario("slowplacetap");

    // 20. hold-then-place: failsafe DOWN at 3x hold, later fingers stay silent
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(7000, 1, a); }
    { const int a[][2] = {{100, 100}}; frame(7300, 1, a); }
    { const int a[][2] = {{100, 100}}; frame(7800, 1, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(7900, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(7916, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(7932, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(7948, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}, {180, 100}}; frame(8100, 3, a); }
    { const int a[][2] = {{100, 100}, {140, 100}, {180, 100}}; frame(8116, 3, a); }
    { const int a[][2] = {{100, 100}, {140, 100}, {180, 100}}; frame(8132, 3, a); }
    { const int a[][2] = {{100, 100}, {140, 100}, {180, 100}}; frame(8148, 3, a); }
    release(8180);
    dumpScenario("holdadd");

    // 21. staggered 2-tap lift: 2-down together, lift one, release -> R click
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(8500, 1, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(8516, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(8532, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(8548, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(8564, 2, a); }
    { const int a[][2] = {{140, 100}}; frame(8580, 1, a); }
    { const int a[][2] = {{140, 100}}; frame(8596, 1, a); }
    release(8628);
    dumpScenario("stag2tap");

    // 22. staggered lift then real move: MOVE streams, tap candidacy dies
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(8600, 1, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(8616, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(8632, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(8648, 2, a); }
    { const int a[][2] = {{100, 100}, {140, 100}}; frame(8680, 2, a); }
    { const int a[][2] = {{140, 100}}; frame(8696, 1, a); }
    { const int a[][2] = {{140, 100}}; frame(8712, 1, a); }
    { const int a[][2] = {{160, 100}}; frame(8728, 1, a); }
    release(8760);
    dumpScenario("stag2move");

    // 23. failsafe long hold: DOWN at 3x holdMs even motionless
    touchpad::reset();
    { const int a[][2] = {{300, 300}}; frame(9000, 1, a); }
    { const int a[][2] = {{300, 300}}; frame(9400, 1, a); }
    { const int a[][2] = {{300, 300}}; frame(9800, 1, a); }
    { const int a[][2] = {{320, 300}}; frame(9816, 1, a); }
    release(9832);
    dumpScenario("failsafe");

    // 30. rapid re-tap at a new spot: two clicks, no teleport (reported bug)
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(10000, 1, a); }
    frame(10016, 0, NULL);
    { const int a[][2] = {{300, 100}}; frame(10032, 1, a); }
    release(10064);
    dumpScenario("retapAB");

    // 31. near-spot flicker still merges: single click
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(10100, 1, a); }
    frame(10116, 0, NULL);
    { const int a[][2] = {{105, 100}}; frame(10132, 1, a); }
    release(10164);
    dumpScenario("nearflicker");

    // 32. gap mid-drag at a new spot: drag ends, fresh tap begins
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(11000, 1, a); }
    { const int a[][2] = {{100, 100}}; frame(11260, 1, a); }
    { const int a[][2] = {{150, 100}}; frame(11276, 1, a); }
    frame(11292, 0, NULL);
    { const int a[][2] = {{400, 100}}; frame(11308, 1, a); }
    release(11340);
    dumpScenario("splitdrag");

    // 33. swipe tail step-down: 3-swipe, lift to 2 with spread change.
    // The step-down pair must NOT lock zoom; release still swipes.
    touchpad::reset();
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(12000, 3, a); }
    { const int a[][2] = {{140, 400}, {180, 400}, {220, 400}}; frame(12016, 3, a); }
    { const int a[][2] = {{180, 400}, {220, 400}, {260, 400}}; frame(12032, 3, a); }
    { const int a[][2] = {{220, 400}, {260, 400}, {300, 400}}; frame(12048, 3, a); }
    { const int a[][2] = {{260, 400}, {300, 400}, {340, 400}}; frame(12064, 3, a); }
    { const int a[][2] = {{300, 400}, {380, 400}}; frame(12080, 2, a); }
    { const int a[][2] = {{300, 400}, {380, 400}}; frame(12096, 2, a); }
    { const int a[][2] = {{280, 400}, {400, 400}}; frame(12112, 2, a); }
    release(12144);
    dumpScenario("swipetail");

    // 34. mid-swipe contact flicker 3->2->3: displacement preserved, no ZOOM.
    // The 2->3 step-up needs 4 frames; the transient triple survives it.
    touchpad::reset();
    { const int a[][2] = {{100, 400}, {140, 400}, {180, 400}}; frame(12200, 3, a); }
    { const int a[][2] = {{140, 400}, {180, 400}, {220, 400}}; frame(12216, 3, a); }
    { const int a[][2] = {{140, 400}, {180, 400}}; frame(12232, 2, a); }
    { const int a[][2] = {{140, 400}, {180, 400}}; frame(12248, 2, a); }
    { const int a[][2] = {{180, 400}, {220, 400}, {260, 400}}; frame(12264, 3, a); }
    { const int a[][2] = {{180, 400}, {220, 400}, {260, 400}}; frame(12280, 3, a); }
    { const int a[][2] = {{180, 400}, {220, 400}, {260, 400}}; frame(12296, 3, a); }
    { const int a[][2] = {{180, 400}, {220, 400}, {260, 400}}; frame(12312, 3, a); }
    { const int a[][2] = {{220, 400}, {260, 400}, {300, 400}}; frame(12328, 3, a); }
    { const int a[][2] = {{260, 400}, {300, 400}, {340, 400}}; frame(12344, 3, a); }
    { const int a[][2] = {{300, 400}, {340, 400}, {380, 400}}; frame(12360, 3, a); }
    release(12392);
    dumpScenario("swipeflicker");

    // 20. 3-finger swipe up: FMOVE stream + one SWIPE on release
    touchpad::reset();
    { const int a[][2] = {{200, 200}, {240, 200}, {280, 200}}; frame(5000, 3, a); }
    { const int a[][2] = {{200, 160}, {240, 160}, {280, 160}}; frame(5016, 3, a); }
    { const int a[][2] = {{200, 120}, {240, 120}, {280, 120}}; frame(5032, 3, a); }
    { const int a[][2] = {{200, 80}, {240, 80}, {280, 80}}; frame(5048, 3, a); }
    { const int a[][2] = {{200, 40}, {240, 40}, {280, 40}}; frame(5064, 3, a); }
    release(5096);
    dumpScenario("threeswipe");

    // 21. short 3-finger move: stream only, below swipeDist -> no SWIPE
    touchpad::reset();
    { const int a[][2] = {{200, 200}, {240, 200}, {280, 200}}; frame(5200, 3, a); }
    { const int a[][2] = {{200, 170}, {240, 170}, {280, 170}}; frame(5216, 3, a); }
    { const int a[][2] = {{200, 150}, {240, 150}, {280, 150}}; frame(5232, 3, a); }
    { const int a[][2] = {{200, 130}, {240, 130}, {280, 130}}; frame(5248, 3, a); }
    { const int a[][2] = {{200, 120}, {240, 120}, {280, 120}}; frame(5264, 3, a); }
    release(5296);
    dumpScenario("threeslow");

    // 22. pinch out then partly back: ZOOM deltas, no FMOVE, no click
    touchpad::reset();
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(5400, 2, a); }
    { const int a[][2] = {{90, 400}, {150, 400}}; frame(5416, 2, a); }
    { const int a[][2] = {{80, 400}, {160, 400}}; frame(5432, 2, a); }
    { const int a[][2] = {{70, 400}, {170, 400}}; frame(5448, 2, a); }
    { const int a[][2] = {{60, 400}, {180, 400}}; frame(5464, 2, a); }
    { const int a[][2] = {{70, 400}, {170, 400}}; frame(5480, 2, a); }
    release(5496);
    dumpScenario("pinch");

    // 23. STAGGERED 3-finger swipe up (the real-hands case): lift 3->2->0,
    // swipe must still fire from session displacement
    touchpad::reset();
    { const int a[][2] = {{200, 200}, {240, 200}, {280, 200}}; frame(5600, 3, a); }
    { const int a[][2] = {{200, 160}, {240, 160}, {280, 160}}; frame(5616, 3, a); }
    { const int a[][2] = {{200, 120}, {240, 120}, {280, 120}}; frame(5632, 3, a); }
    { const int a[][2] = {{200, 80}, {240, 80}, {280, 80}}; frame(5648, 3, a); }
    { const int a[][2] = {{200, 40}, {240, 40}, {280, 40}}; frame(5664, 3, a); }
    { const int a[][2] = {{200, 40}, {240, 40}}; frame(5680, 2, a); }
    { const int a[][2] = {{200, 40}, {240, 40}}; frame(5696, 2, a); }
    release(5728);
    dumpScenario("staggeredswipe");

    // 24. STAGGERED 3-finger tap: 3-down, lift to 2, release -> middle click
    touchpad::reset();
    { const int a[][2] = {{200, 300}, {240, 300}, {280, 300}}; frame(5800, 3, a); }
    { const int a[][2] = {{200, 300}, {240, 300}}; frame(5816, 2, a); }
    { const int a[][2] = {{200, 300}, {240, 300}}; frame(5832, 2, a); }
    release(5864);
    dumpScenario("staggeredtap");

    // 25. 3-finger scroll, step down to 2, release: swipe fires (session rule)
    touchpad::reset();
    { const int a[][2] = {{200, 400}, {240, 400}, {280, 400}}; frame(5900, 3, a); }
    { const int a[][2] = {{200, 440}, {240, 440}, {280, 440}}; frame(5916, 3, a); }
    { const int a[][2] = {{200, 480}, {240, 480}, {280, 480}}; frame(5932, 3, a); }
    { const int a[][2] = {{200, 520}, {240, 520}, {280, 520}}; frame(5948, 3, a); }
    { const int a[][2] = {{200, 560}, {240, 560}, {280, 560}}; frame(5964, 3, a); }
    { const int a[][2] = {{200, 560}, {240, 560}}; frame(5980, 2, a); }
    { const int a[][2] = {{200, 560}, {240, 560}}; frame(5996, 2, a); }
    release(6028);
    dumpScenario("scrollstepdown");

    // 35. diagonal pinch with centroid drift: radial dominates -> ZOOM only.
    // Scroll must NOT lock even though the centroid travels past slop.
    touchpad::reset();
    { const int a[][2] = {{100, 400}, {200, 400}}; frame(13000, 2, a); }
    { const int a[][2] = {{90, 390}, {215, 415}}; frame(13016, 2, a); }
    { const int a[][2] = {{80, 380}, {230, 430}}; frame(13032, 2, a); }
    { const int a[][2] = {{70, 370}, {245, 445}}; frame(13048, 2, a); }
    { const int a[][2] = {{60, 360}, {260, 460}}; frame(13064, 2, a); }
    { const int a[][2] = {{50, 350}, {275, 475}}; frame(13080, 2, a); }
    release(13112);
    dumpScenario("radialzoom");

    // 36. grip settle then scroll: radial blip below confirmation, then scroll.
    // Must stream FMOVE only — never ZOOM.
    touchpad::reset();
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(13200, 2, a); }
    { const int a[][2] = {{85, 400}, {155, 400}}; frame(13216, 2, a); }
    { const int a[][2] = {{75, 400}, {165, 400}}; frame(13232, 2, a); }
    { const int a[][2] = {{105, 400}, {195, 400}}; frame(13248, 2, a); }
    { const int a[][2] = {{135, 400}, {225, 400}}; frame(13264, 2, a); }
    { const int a[][2] = {{165, 400}, {255, 400}}; frame(13280, 2, a); }
    release(13312);
    dumpScenario("settlescroll");

    // 37. singleton merge flicker: 2,1(same centroid),2... never switches phase
    touchpad::reset();
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(13400, 2, a); }
    { const int a[][2] = {{140, 440}, {180, 440}}; frame(13416, 2, a); }
    { const int a[][2] = {{160, 440}}; frame(13432, 1, a); }
    { const int a[][2] = {{180, 480}, {220, 480}}; frame(13448, 2, a); }
    { const int a[][2] = {{220, 520}, {260, 520}}; frame(13464, 2, a); }
    release(13496);
    dumpScenario("mergeflicker");

    // 38. seeded triple vote: undecided 2-motion carries into the 3-swipe.
    // 3-portion alone (96) is below swipeDist(100); with the (4,4) seed it fires.
    // Both step-ups need 4 identical frames under asymmetric confirmation.
    touchpad::reset();
    { const int a[][2] = {{100, 400}}; frame(13600, 1, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(13616, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(13632, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(13648, 2, a); }
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(13664, 2, a); }
    { const int a[][2] = {{104, 404}, {144, 404}}; frame(13680, 2, a); }
    { const int a[][2] = {{104, 404}, {144, 404}, {144, 444}}; frame(13696, 3, a); }
    { const int a[][2] = {{104, 404}, {144, 404}, {144, 444}}; frame(13712, 3, a); }
    { const int a[][2] = {{104, 404}, {144, 404}, {144, 444}}; frame(13728, 3, a); }
    { const int a[][2] = {{104, 404}, {144, 404}, {144, 444}}; frame(13744, 3, a); }
    { const int a[][2] = {{128, 428}, {168, 428}, {168, 468}}; frame(13760, 3, a); }
    { const int a[][2] = {{152, 452}, {192, 452}, {192, 492}}; frame(13776, 3, a); }
    release(13808);
    dumpScenario("seededswipe");

    // 43. phantom second finger mid-motion (<65ms): ignored, stream continues.
    // The reported flap: MOVE, brief FMOVE-less gap, MOVE — never FMOVE.
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(15000, 1, a); }
    { const int a[][2] = {{140, 100}}; frame(15016, 1, a); }
    { const int a[][2] = {{180, 100}}; frame(15032, 1, a); }
    { const int a[][2] = {{180, 100}, {220, 100}}; frame(15048, 2, a); }
    { const int a[][2] = {{180, 100}, {220, 100}}; frame(15064, 2, a); }
    { const int a[][2] = {{180, 100}, {220, 100}}; frame(15080, 2, a); }
    { const int a[][2] = {{220, 100}}; frame(15096, 1, a); }
    { const int a[][2] = {{260, 100}}; frame(15112, 1, a); }
    release(15144);
    dumpScenario("phantom2");

    // 39. short swipe (~110 units): fires under the new default 100
    touchpad::reset();
    { const int a[][2] = {{200, 300}, {240, 300}, {280, 300}}; frame(13800, 3, a); }
    { const int a[][2] = {{200, 260}, {240, 260}, {280, 260}}; frame(13816, 3, a); }
    { const int a[][2] = {{200, 220}, {240, 220}, {280, 220}}; frame(13832, 3, a); }
    { const int a[][2] = {{200, 190}, {240, 190}, {280, 190}}; frame(13848, 3, a); }
    release(13880);
    dumpScenario("shortswipe");

    // 40. sub-threshold 3-motion: streams, never swipes
    touchpad::reset();
    { const int a[][2] = {{200, 300}, {240, 300}, {280, 300}}; frame(13900, 3, a); }
    { const int a[][2] = {{200, 285}, {240, 285}, {280, 285}}; frame(13916, 3, a); }
    { const int a[][2] = {{200, 270}, {240, 270}, {280, 270}}; frame(13932, 3, a); }
    release(13964);
    dumpScenario("sub40");

    // 41. sustained merge mid-scroll: blob near centroid keeps FMOVE(2) flowing.
    // No MOVE, no phase-switch artifacts, single END.
    touchpad::reset();
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(15000, 2, a); }
    { const int a[][2] = {{140, 440}, {180, 440}}; frame(15016, 2, a); }
    { const int a[][2] = {{180, 480}, {220, 480}}; frame(15032, 2, a); }
    { const int a[][2] = {{200, 480}}; frame(15048, 1, a); }
    { const int a[][2] = {{200, 480}}; frame(15064, 1, a); }
    { const int a[][2] = {{240, 520}}; frame(15080, 1, a); }
    { const int a[][2] = {{280, 560}}; frame(15096, 1, a); }
    { const int a[][2] = {{300, 600}, {340, 600}}; frame(15112, 2, a); }
    { const int a[][2] = {{340, 640}, {380, 640}}; frame(15128, 2, a); }
    release(15160);
    dumpScenario("mergescroll");

    // 42. merge-hold-release with a clean bout: right click, not silence
    touchpad::reset();
    { const int a[][2] = {{100, 400}, {140, 400}}; frame(15200, 2, a); }
    { const int a[][2] = {{120, 400}}; frame(15216, 1, a); }
    { const int a[][2] = {{120, 400}}; frame(15232, 1, a); }
    release(15264);
    dumpScenario("mergetap");

    // 43. jump cap: 200-unit single-frame teleport absorbed, stream resumes.
    // Release after real motion: silence (moved), proving the jump didn't click.
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(16000, 1, a); }
    { const int a[][2] = {{300, 100}}; frame(16200, 1, a); }
    { const int a[][2] = {{320, 100}}; frame(16216, 1, a); }
    release(16248);
    dumpScenario("jumpcap");

    // 44. close landing then split inside the window: zero MOVE ever.
    // Pre-split motion is discarded (not flushed); scroll streams clean.
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(16300, 1, a); }
    { const int a[][2] = {{120, 100}}; frame(16316, 1, a); }
    { const int a[][2] = {{120, 100}, {160, 100}}; frame(16332, 2, a); }
    { const int a[][2] = {{120, 100}, {160, 100}}; frame(16348, 2, a); }
    { const int a[][2] = {{120, 100}, {160, 100}}; frame(16364, 2, a); }
    { const int a[][2] = {{120, 100}, {160, 100}}; frame(16380, 2, a); }
    { const int a[][2] = {{140, 120}, {180, 120}}; frame(16396, 2, a); }
    release(16428);
    dumpScenario("holdclose");

    // 45. fast flick: over-cap deltas that continue a fast same-direction
    // streak pass through (only isolated teleports are absorbed). Motion starts
    // below the cap, then steps to 130/140/frame; every unit of travel is kept.
    touchpad::reset();
    { const int a[][2] = {{100, 100}}; frame(17000, 1, a); }
    { const int a[][2] = {{200, 100}}; frame(17016, 1, a); }
    { const int a[][2] = {{320, 100}}; frame(17032, 1, a); }
    { const int a[][2] = {{450, 100}}; frame(17048, 1, a); }
    { const int a[][2] = {{590, 100}}; frame(17064, 1, a); }
    { const int a[][2] = {{730, 100}}; frame(17080, 1, a); }
    { const int a[][2] = {{870, 100}}; frame(17096, 1, a); }
    release(17128);
    dumpScenario("flick");

    return 0;
}
