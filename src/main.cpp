#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <vita2d.h>
#include <imgui_vita2d/imgui_vita.h>

#include "app.h"
#include "config.h"
#include "ime.h"
#include "measure.h"
#include "protocol.h"
#include "screen.h"
#include "touchpad.h"
#include "udp.h"
#include "ui/touchpad.h"
#include "ui/settings.h"

#define SCREEN_WIDTH  960
#define SCREEN_HEIGHT 544

static void boot_log(const char *msg) {
    sceIoMkdir("ux0:data/VitaTouchPad", 0777);
    int fd = sceIoOpen("ux0:data/VitaTouchPad/boot.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static void ime_log(const char *msg) {
    int fd = sceIoOpen("ux0:data/VitaTouchPad/ime.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

// Non-interactive splash drawn UNDER the system dialog. If the dialog renders,
// it covers this completely. If the dialog fails, the user sees proof the app
// is alive instead of a black screen (and Circle still cancels the dialog).
static void drawImeOverlay(AppState& app) {
    const char* field = "unknown";
    if (app.imeField == ImeField::ServerIp) field = "Server IP address";
    else if (app.imeField == ImeField::ServerPort) field = "Server UDP port";
    else if (app.imeField == ImeField::Passcode) field = "Passcode";

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(960, 544));
    ImGui::Begin("ImeOverlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoInputs);
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text("System keyboard open: %s", field);
    ImGui::TextDisabled("Type in the system dialog, or press Circle to cancel.");
    ImGui::End();
}

static void sendHello(AppState& app) {
    if (!udp::isUp()) return;
    if (!udp::setDestination(app.config.serverIp, app.config.serverPort)) return;
    uint8_t mode = app.scrollMode ? vtp::MODE_SCROLL : vtp::MODE_CURSOR;
    udp::sendHello(mode, app.config.startInScrollMode, app.config.passcode);
}

// Touchpad-screen brightness: full while debugging (the feed must be readable),
// battery saver otherwise.
static void applyTouchpadBrightness(AppState& app) {
    if (app.config.showEventLog) {
        screen::apply(screen::FULL, app.config.dimLevel);
    } else {
        screen::applyBattery(app.config);
    }
}

// Vita buttons forwarded to the PC as VPAD edge events for shortcut mapping.
// SELECT and START are local-only (settings navigation, scroll-mode toggle).
struct PadMapEntry { unsigned sce; uint8_t vtp; };
static const PadMapEntry kPadMap[] = {
    { SCE_CTRL_CROSS,    vtp::VITA_CROSS },
    { SCE_CTRL_CIRCLE,   vtp::VITA_CIRCLE },
    { SCE_CTRL_SQUARE,   vtp::VITA_SQUARE },
    { SCE_CTRL_TRIANGLE, vtp::VITA_TRIANGLE },
    { SCE_CTRL_LTRIGGER, vtp::VITA_L1 },
    { SCE_CTRL_RTRIGGER, vtp::VITA_R1 },
    { SCE_CTRL_UP,       vtp::VITA_DPAD_UP },
    { SCE_CTRL_DOWN,     vtp::VITA_DPAD_DOWN },
    { SCE_CTRL_LEFT,     vtp::VITA_DPAD_LEFT },
    { SCE_CTRL_RIGHT,    vtp::VITA_DPAD_RIGHT },
};

// Sends DOWN/UP for every mapped button whose state changed this frame.
// Call once per frame while on the touchpad screen.
static void sendPadEdges(AppState& app) {
    if (!udp::hasDestination()) return; // don't spam the error counter unconfigured
    for (size_t i = 0; i < sizeof(kPadMap) / sizeof(kPadMap[0]); i++) {
        bool was = (app.input.prevPad.buttons & kPadMap[i].sce) != 0;
        bool is = (app.input.pad.buttons & kPadMap[i].sce) != 0;
        if (is == was) continue;
        udp::sendVPad(kPadMap[i].vtp, is ? vtp::ACTION_DOWN : vtp::ACTION_UP,
                      app.config.passcode);
    }
}

// Sends UP for every mapped button currently held. Called when leaving the
// touchpad screen and on shutdown so the PC never keeps a stuck shortcut key.
static void releasePadHeld(AppState& app) {
    if (!udp::hasDestination()) return;
    for (size_t i = 0; i < sizeof(kPadMap) / sizeof(kPadMap[0]); i++) {
        if (app.input.pad.buttons & kPadMap[i].sce) {
            udp::sendVPad(kPadMap[i].vtp, vtp::ACTION_UP, app.config.passcode);
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    boot_log("== boot ==");

    sceIoMkdir(config::dir(), 0777);

    AppState app;
    memset(&app.input.pad, 0, sizeof(app.input.pad));
    memset(&app.input.prevPad, 0, sizeof(app.input.prevPad));
    memset(&app.input.touch, 0, sizeof(app.input.touch));

    if (!config::load(app.config)) {
        boot_log("config missing, seeding default");
        config::seedDefault(app.config);
        config::save(app.config);
    } else {
        config::seedDefault(app.config); // fill any missing/out-of-range fields
        config::save(app.config);
    }
    app.scrollMode = app.config.startInScrollMode;
    boot_log("config loaded");

    bool recording = app.config.measureMode;
    if (recording) {
        if (measure::init()) {
            boot_log("measure recording on");
        } else {
            boot_log("WARN: measure init failed");
            recording = false;
        }
    }

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    // Panel info is the ground truth for Phase 2/3 coordinate work; log it now.
    if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &app.input.panelInfo) == 0) {
        app.input.panelInfoOk = true;
        char buf[128];
        snprintf(buf, sizeof(buf), "panel aa=(%d,%d)-(%d,%d) disp=(%d,%d)-(%d,%d)",
                 app.input.panelInfo.minAaX, app.input.panelInfo.minAaY,
                 app.input.panelInfo.maxAaX, app.input.panelInfo.maxAaY,
                 app.input.panelInfo.minDispX, app.input.panelInfo.minDispY,
                 app.input.panelInfo.maxDispX, app.input.panelInfo.maxDispY);
        boot_log(buf);
    } else {
        boot_log("WARN: sceTouchGetPanelInfo failed");
    }

    vita2d_init();
    vita2d_set_vblank_wait(true);
    boot_log("vita2d inited");

    ImGui::CreateContext();
    boot_log("imgui context created");

    // Add fonts BEFORE the imgui_vita2d backend init: ImGui_ImplVita2D_Init()
    // builds the font atlas immediately, so fonts added later never rasterize.
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImFont *fontUi = io.Fonts->AddFontFromFileTTF("app0:/Literata.ttf", 24.0f, NULL,
                                                  io.Fonts->GetGlyphRangesDefault());
    if (fontUi) {
        boot_log("literata font loaded (24px UI)");
    } else {
        boot_log("WARN: literata font failed to load, using default");
    }

    ImGui_ImplVita2D_Init();
    // Touch ownership is per-screen (set every frame before NewFrame):
    // - Settings: backend touch ON so fields/buttons are tappable.
    // - Touchpad: backend touch OFF; touch is read raw via sceTouchPeek
    //   (Phase 2's gesture engine will own it the same way).
    ImGui_ImplVita2D_UseIndirectFrontTouch(false);
    ImGui_ImplVita2D_UseRearTouch(false);
    // Analog stick as mouse is disabled so buttons/stick are free for app-level
    // input; the stick drives dpad nav with a sane repeat rate instead.
    ImGui_ImplVita2D_MouseStickUsage(false);
    ImGui_ImplVita2D_GamepadUsage(true);
    ImGui_ImplVita2D_SetAnalogRepeatDelay(200000);
    boot_log("imgui backend inited");

    if (!udp::init()) {
        boot_log("WARN: udp/net init failed");
    } else {
        boot_log("udp inited");
    }

    if (!ime::init()) {
        boot_log("WARN: ime init failed");
    } else {
        boot_log("ime inited");
    }

    screen::init();
    char brightMsg[64];
    snprintf(brightMsg, sizeof(brightMsg), "max brightness=%d", screen::maxBrightness());
    boot_log(brightMsg);

    // Start on the touchpad screen: battery mode + announce ourselves.
    applyTouchpadBrightness(app);
    sendHello(app);
    boot_log("hello sent");

    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    SceTouchData touch;
    memset(&touch, 0, sizeof(touch));
    AppScreen prevScreen = app.screen;
    unsigned frameNo = 0;
    bool quit = false;

    while (!quit) {
        app.input.prevPad = app.input.pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        app.input.pad = pad;

        memset(&touch, 0, sizeof(touch));
        app.input.touchRet = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        if (app.input.touchRet >= 0) {
            app.input.touch = touch;
        } else {
            memset(&app.input.touch, 0, sizeof(app.input.touch));
        }
        if (recording) {
            measure::logFrame(sceKernelGetProcessTimeLow() / 1000,
                              app.input.touch, touchpad::phaseId());
        }

        // Screen transitions drive the backlight + (re)connect + gestures.
        if (app.screen != prevScreen) {
            if (app.screen == AppScreen::Settings) {
                releasePadHeld(app); // don't leave PC shortcut keys stuck
                touchpad::suspend(app); // release any held button first
                screen::apply(screen::FULL, app.config.dimLevel);
            } else {
                config::save(app.config); // persist any edits made in Settings
                touchpad::reset();
                applyTouchpadBrightness(app);
                sendHello(app); // destination may have changed
            }
            prevScreen = app.screen;
        }

        vita2d_start_drawing();
        vita2d_clear_screen();

        ImGui_ImplVita2D_TouchUsage(app.screen == AppScreen::Settings);
        ImGui_ImplVita2D_NewFrame();

        if (ime::isOpen()) {
            ime::update();
            drawImeOverlay(app);
        } else {
            // Button navigation (gamepad + touch both work in Settings).
            if (pressed(app.input.pad, app.input.prevPad, SCE_CTRL_SELECT)) {
                app.screen = (app.screen == AppScreen::Touchpad) ? AppScreen::Settings
                                                                 : AppScreen::Touchpad;
            }
            if (app.screen == AppScreen::Settings &&
                pressed(app.input.pad, app.input.prevPad, SCE_CTRL_CIRCLE)) {
                app.screen = AppScreen::Touchpad;
            }
            // START toggles scroll mode on the touchpad screen.
            if (app.screen == AppScreen::Touchpad &&
                pressed(app.input.pad, app.input.prevPad, SCE_CTRL_START)) {
                app.scrollMode = !app.scrollMode;
                app.config.startInScrollMode = app.scrollMode;
                config::save(app.config);
                if (udp::setDestination(app.config.serverIp, app.config.serverPort)) {
                    uint8_t mode = app.scrollMode ? vtp::MODE_SCROLL : vtp::MODE_CURSOR;
                    udp::sendMode(mode, app.config.passcode);
                }
            }

            switch (app.screen) {
                case AppScreen::Settings: quit = drawSettings(app); break;
                case AppScreen::Touchpad:
                default:
                    touchpad::update(app);
                    touchpad::updateStick(app);
                    sendPadEdges(app);
                    quit = drawTouchpad(app);
                    break;
            }
        }

        ImGui::Render();
        ImGui_ImplVita2D_RenderDrawData(ImGui::GetDrawData());

        vita2d_end_drawing();

        // The system dialog renders into the back buffer itself (see the
        // official ime sample: no app GXM scene is open around the update).
        // Calling it mid-scene is suspected of silently breaking dialog
        // composition, so it runs after end_drawing from v8 on.
        static int dlgFrames = 0;
        static int dlgLastRc = 0x7fffffff;
        if (ime::isOpen()) {
            int rc = vita2d_common_dialog_update();
            if (rc != dlgLastRc || (dlgFrames % 300) == 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "dialog update: rc=0x%08X frame=%d",
                         (unsigned)rc, dlgFrames);
                ime_log(buf);
                dlgLastRc = rc;
            }
        }
        dlgFrames++;

        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();

        // Re-assert the touchpad brightness periodically: this recovers the
        // dim/off state after suspend/resume (which resets brightness) and any
        // system interference, without needing power callbacks.
        if (app.screen == AppScreen::Touchpad && (frameNo % 300) == 0) {
            applyTouchpadBrightness(app);
        }
        frameNo++;
    }

    boot_log("clean shutdown");
    config::save(app.config);
    if (recording) measure::shutdown();
    releasePadHeld(app); // never leave PC shortcut keys stuck down
    touchpad::suspend(app); // never leave the PC button stuck down
    screen::shutdown(); // restore full brightness
    udp::shutdown();
    ImGui_ImplVita2D_Shutdown();
    ImGui::DestroyContext();
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
