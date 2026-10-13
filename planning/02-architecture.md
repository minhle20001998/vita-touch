# 02 — Architecture

## Module layout

```
src/
  main.cpp            init (vita2d, imgui, net, ime), main loop, screen manager, buttons
  app.h               global AppState: screen, config, touchpad mode, connection info
  config.h/.cpp       load/save ux0:data/VitaTouchPad/config.json (atomic write)
  udp.h/.cpp          SceNet UDP socket wrapper: init, send(packet), shutdown
  protocol.h          packed packet structs + event type / button constants + helpers
  touchpad.h/.cpp     raw touch sampling → gesture state machine → protocol events
  screen.h/.cpp       backlight wrapper: apply(dim|off|full), save/restore
  ime.h/.cpp          system on-screen keyboard (reused from vita-reader)
  ui/touchpad.cpp     main screen: mode/connection indicator, dim overlay
  ui/settings.cpp     settings form (gamepad nav) + IME fields + Test/Save
vendor/
  fonts/Literata.ttf  bundled into the VPK
assets/livearea/      icon0.png, bg.png, startup.png, template.xml
scripts/version_vpk.sh
```

Rendering/UI uses the SDK's `imgui_vita2d` (landscape 960×544). No portrait render target
is needed. There is **no worker thread**: UDP sends are non-blocking and issued from the
main thread. (If profiling shows send jitter, a small dedicated send queue/thread can be
added later behind the same `udp::send` API.)

## Main loop (sketch)

```
storage/config load → seed defaults on first run
sceCtrlSetSamplingMode(ANALOG_WIDE)
sceTouchSetSamplingState(FRONT, START)
vita2d_init; ImGui context; fonts; ImGui_ImplVita2D_Init
ImGui_ImplVita2D_TouchUsage(false)   // gesture engine owns touch
udp::init()
ime::init()
screen::save()                        // remember current brightness
enterTouchpad()                       // apply battery mode

while (!quit):
    sceCtrlPeekBufferPositive(&pad, 1)
    ImGui_ImplVita2D_NewFrame()
    if ime::isOpen(): ime::update()
    else if screen == Settings: ui::drawSettings(app)      // full backlight
    else:
        touchpad::update(app, pad)   // reads touch, emits UDP events
        ui::drawTouchpad(app)
        if pad pressed START: toggle scroll mode (+ MODE packet)
        if pad pressed SELECT: open Settings (restore backlight)
    ImGui::Render(); ImGui_ImplVita2D_RenderDrawData(...)
    if ime::isOpen(): vita2d_common_dialog_update()
    udp::flush()                     // no-op / stats
    ime::poll()
    sceDisplayWaitVblankStart()

restore brightness; udp::shutdown; ime done; ImGui shutdown; vita2d_fini; exit
```

## Networking (`udp.cpp`)

Non-blocking UDP client socket. Send-only; no receive path required by the protocol
(optional `PING`/`HELLO` may be sent periodically for server OSD / debugging).

- Module load: `sceSysmoduleLoadModule(SCE_SYSMODULE_NET)`; `sceNetInit(...)`.
- Socket: `sceNetSocket("vtp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0)`.
- Destination: `sceNetInetPton(SCE_NET_AF_INET, ip, &sin_addr)`; `sin_port = htons(port)`.
- Send: `sceNetSendto(fd, &pkt, len, 0, &addr, sizeof(addr))` per event datagram.
- On config change (IP/port), re-resolve destination; socket stays open.
- Failure handling: count consecutive send errors; surface "disconnected" on the touchpad
  indicator; never block or crash.
- Reuse the NetCtl check (from vita-reader) to show Wi-Fi status if needed.

## Gesture engine (`touchpad.cpp`)

State machine fed each frame by `sceTouchPeek(SCE_TOUCH_PORT_FRONT, ...)`.

**Sampling / coordinates**
- Report coords are panel units; convert deltas directly (relative motion is
  resolution-independent). Optionally normalize by `sceTouchGetPanelInfo` active area.
- Apply `sensitivity` (cursor) / `scrollSensitivity` multipliers; clamp to `int16_t`.

**Multi-touch counting**
- Track the number of active fingers. Transition 0→1 starts a one-finger gesture;
  transition 1→2 aborts any in-progress one-finger click/drag and starts a two-finger
  gesture; 2→0 ends it.
- **Contact-count changes re-anchor without emitting.** If the count changes mid-scroll
  (2↔3, grazed palm, staggered landing), the centroid jumps discontinuously, so the
  engine re-takes its anchor and **disarms the release-click**: contact juggling can
  never click. Only a clean down→up with no juggling taps.
- **Release debounce + re-touch split.** A release takes 3 consecutive empty frames
  (~50 ms): a single dropped panel frame must not end the gesture. But a contact
  reappearing inside that window is either flicker or a fast re-tap: within tap
  slop of the old anchor it resumes the bout silently, beyond slop the old bout
  is completed immediately (tap/click fires) and a fresh bout begins. Tap-A-lift-
  tap-B therefore yields two clicks with zero cursor teleport, and a mid-drag gap
  ends the drag before starting the new touch.
- **Count-change confirmation, asymmetric.** A change between active counts takes
  effect only after consecutive confirmation frames — **4 for adding contacts**,
  2 for removing them. Grazes, hovering fingers, and palm brushes rarely survive
  ~65 ms, so phantom step-ups never switch phases (a one-finger move stays pure
  `MOVE` instead of flapping to `FMOVE` and back); genuine additions always do.
  Fresh touches from IDLE stay instant; release keeps its own 3-frame debounce.
  Cost: deliberate mid-motion finger additions start ~30 ms later than before —
  invisible against the flap it kills.
- **Touchdown hold (anti-teleport).** Fresh-bout cursor motion accumulates silently
  for `landingHoldMs` (default 80, tunable 0–200, `0` disables). A count-up inside
  the window discards it (close landing → clean switch, zero cursor jump);
  otherwise it flushes as one catch-up `MOVE` (totals bit-exact). Taps, slop, locks,
  and drag timing run on live positions underneath, so honesty is unaffected.
- **Per-frame jump cap with flick continuity (120).** An over-cap single-frame
  centroid delta is absorbed (re-anchor, no emit, no accrual) only when it is an
  *isolated* teleport: the previous frame was slow/at rest, the direction
  reversed, or the magnitude is wildly larger (`>5×` the cap). An over-cap delta
  that continues a fast, same-direction streak is genuine motion and passes
  through untouched, so quick flicks and flings stream every frame while
  fusion-jitter teleports die on contact.
- **Pinch = radial dominance, sustained.** Per-finger motion is projected onto the
  inter-finger axis (translation cancels out); zoom locks only after 3 consecutive
  radially-dominant frames past `pinchSlop`, and only from a clean two-finger
  landing (never from a 3→2 step-down tail). Grip settling and drift can't lock it.
- Motion requires exceeding `tapSlop` (config, default 12); the release-click additionally
  requires the gesture to be tap-armed (fresh down, no juggling since).
- **Stream termination.** Every `FMOVE`/`ZOOM` bout the server could have seen is
  closed with an explicit `END{ended_type, fingers}` — on finger lift, on bout
  switches (2→1, 1→2, 2↔3), on screen leave, and on shutdown. Only bouts that
  actually streamed get one (taps and undecided bouts send none), so the server
  can do press/release mappings with a generous backstop timeout instead of
  timeout-guessing.

**One-finger gesture (cursor mode)**
- Motion past slop streams `MOVE`; a release on a motionless, press-free bout taps.
- **Two-stage hold-drag** (no premature DOWN): a motionless hold sends *nothing*
  until either first motion after `holdMs` (deferred `LEFT DOWN` + `MOVE` together —
  identical on the wire to firing at hold time) or a failsafe `LEFT DOWN` at
  3×`holdMs` (so long-press mappings keep working). Slow multi-finger placements
  therefore never emit phantom clicks.
- **Tap honesty (session rule):** no clicks after any button-press in the session.
  `sessionHadButton` latches on `LEFT DOWN` and clears on release/suspend/fresh
  down; late-joining fingers in such sessions stay silent on release.
- **Lift-stagger inheritance:** a clean 2-tap landing whose fingers lift staggered
  keeps a `pendingTap` — the remaining finger releases to a right-click if it
  stays within slop, or converts to a normal drag/move on motion or hold.

**One-finger gesture (scroll mode)**
- Same detection, but motion emits `FMOVE(fingers=1)` instead of `MOVE`
  (server maps it to scroll by default); tap still = left click.

**Two-finger gesture**
- Scroll-vs-zoom locks per bout: centroid displacement past tap slop →
  `FMOVE(fingers=2)` stream; spread change past `pinchSlop` → `ZOOM` stream
  (scroll wins exact ties, the loser never emits for that bout).
- Release on a clean, unmoved bout → **right click** (down + up).

**Three-finger gesture**
- Motion streams `FMOVE(fingers=3)` (server: unassigned by default).
- Release on a clean, unmoved bout → **middle click** (down + up).
- Release after traveling past `swipeDist` along a dominant axis → one discrete
  `SWIPE(fingers, direction)` (mac-style: up/down/left/right, any speed).
  Undecided two-finger landing motion seeds the vote; locked-scroll bouts start fresh.
- 4+ contacts collapse to the three-finger rows (real count still reported).

**Tunables (config, tuned on hardware):** `sensitivity`, `scrollSensitivity`,
`tapSlop`, `holdMs`, `swipeDist`, `pinchSlop`, `stickSpeed`.

## Protocol (`protocol.h`)

Fixed 32-byte header + small payload, packed (`#pragma pack(1)`), little-endian. Full
spec, byte layout, and reference server pseudocode: [04-wire-protocol.md](04-wire-protocol.md).

Event types: `HELLO`, `MOVE`, `FMOVE`, `BUTTON`, `MODE`, `VPAD`, `SWIPE`, `ZOOM`,
(`PING`).
Buttons: `LEFT=1`, `RIGHT=2`, `MIDDLE=3`. Action: `UP=0`, `DOWN=1`. Modes: `CURSOR=0`,
`SCROLL=1`. Swipe dirs: `UP=1`, `DOWN=2`, `LEFT=3`, `RIGHT=4`.

Every packet carries the passcode field + a monotonic `seq` + `timestampMs`.

## Backlight (`screen.cpp`)

- `sceAVConfigGetDisplayMaxBrightness(&max)` once at startup (fallback 65536).
- `sceAVConfigSetDisplayBrightness(level)`:
  - full: `max`
  - dim: `max(config.dimLevel, 21)` (floor is 21; 0 = off)
  - off: `0`
- `screen::save()` remembers the value before the app takes over; `screen::restore()`
  sets it back on exit.
- Enter Settings → `full`; leave Settings → re-apply battery mode.
- Link `SceAVConfig_stub`. Failures are logged and ignored.

## Config (`config.cpp`)

File: `ux0:data/VitaTouchPad/config.json`. Atomic write (temp + rename), same as
vita-reader's storage pattern.

```json
{
  "serverIp": "192.168.1.100",
  "serverPort": 31415,
  "passcode": "",
  "sensitivity": 1.0,
  "scrollSensitivity": 1.5,
  "batteryMode": "dim",
  "dimLevel": 21,
  "startInScrollMode": false,
  "tapSlop": 12,
  "holdMs": 250,
  "stickSpeed": 600,
  "showEventLog": false,
  "swipeDist": 100,
  "pinchSlop": 40,
  "landingHoldMs": 80
}
```

Host-side verification lives in `host-test/` (`run.sh` runs all three suites):
protocol byte checks, scripted gesture frames with fake time, and config
round-trip against a stubbed filesystem — no hardware needed.

First run seeds sensible defaults (empty IP → indicator shows "not configured").

## Keyboard (`ime.cpp`)

Reused from vita-reader: `SceImeDialog` for IP / port / passcode entry. While
the IME is open, the touchpad gesture engine is paused and the dialog is advanced with
`vita2d_common_dialog_update()`.

**Critical ordering (this caused a black-screen bug on hardware):** the dialog update
must run **after `vita2d_end_drawing()`**, not inside an open GXM scene, then
`vita2d_swap_buffers()`:

```cpp
vita2d_start_drawing();
vita2d_clear_screen();
// ... draw app UI (incl. any overlay under the dialog) ...
vita2d_end_drawing();
int rc = vita2d_common_dialog_update();   // AFTER end_drawing
vita2d_swap_buffers();
sceDisplayWaitVblankStart();
```

This matches VitaShell's `endDrawing()` (`utils.c`) and the vitasdk `ime` sample (which
opens no app scene around `sceCommonDialogUpdate`). Calling the update while a scene is
open makes the dialog silently never compose — `sceImeDialogGetStatus()` stays
`RUNNING` forever and the screen shows only the cleared (black) buffer.

## Error handling & states

- **Not configured** (empty IP): touchpad still runs but shows "set server in Settings";
  packets are dropped.
- **Disconnected**: N consecutive send failures → red indicator + hint.
- **Brightness failure**: logged; app continues at whatever brightness the system allows.
- **Suspend/resume**: re-apply battery mode on resume (verify on hardware).
