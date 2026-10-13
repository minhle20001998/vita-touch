# 01 — Requirements

## Goal

Turn a PS Vita into a wireless pointing device for a PC: sample the front touch panel,
interpret gestures, and emit mouse/scroll events to a PC-side server over UDP.

## Confirmed behaviour

1. **Touchpad mode (main screen)**
   - Touching / dragging / tapping the front touch panel produces mouse events sent to the
     PC over UDP (the PC is assumed to run a server bound to a known UDP port).
   - Movement is **relative** (delta per sample), not absolute screen mapping.
2. **Battery saving on the main screen**
   - Backlight is **dimmed to the minimum** by default; **fully off** is an option.
   - Front touch sampling stays **active** in all cases; the surface keeps working blindly.
3. **START button → scroll mode**
   - Toggles the touchpad between **cursor mode** and **scroll mode**.
   - One-finger drag scrolls in scroll mode (instead of moving the cursor).
   - The toggle is also reported to the PC server via a `MODE` packet.
   - A visible indicator shows the active mode.
4. **SELECT button → Settings**
   - Opens a settings screen; restores full backlight while open.
   - Lets the user enter the **server IP**, **port**, and **passcode** (system on-screen
     keyboard), plus sensitivity and battery preference.
   - Persists settings to `ux0:data/VitaTouchPad/config.json`.

## Gestures (confirmed)

| Gesture | Emitted event |
|---------|---------------|
| One finger drag | `MOVE` (relative `dx,dy`) — or `SCROLL` when in scroll mode |
| One finger tap (short, no movement) | `BUTTON` left down + up (left click) |
| One finger tap-hold then drag | `BUTTON` left down, then `MOVE`, then `BUTTON` left up (drag) |
| Two-finger drag | `SCROLL` (`dx,dy`) — works in either mode |
| Two-finger tap | `BUTTON` right down + up (right click) |
| START | `MODE` packet + local mode switch |
| SELECT | open Settings (local only) |

## Controls

| Input | Action |
|-------|--------|
| Front touch | gestures above |
| START | toggle scroll mode |
| SELECT | open Settings |
| Circle (in Settings) | leave Settings → back to touchpad |
| PS button / system | exit app (handled by system) |

## Decisions & assumptions

- **PC server exists.** This project defines the wire protocol only; the companion server
  is out of scope ([04-wire-protocol.md](04-wire-protocol.md)).
- **Plain passcode** in every packet (fixed 16-byte field). Trusted-LAN threat model.
- **Compact binary** datagrams (little-endian), not JSON, for low per-packet overhead.
- **Relative trackpad** semantics — the Vita applies a sensitivity multiplier; the server
  injects the delta verbatim (no server-side scaling config required).
- **Port default 31415**, configurable.
- **Single-touch + two-finger** only; no three-finger gestures in scope.
- The app is **landscape-only** (no portrait rendering path needed).
- ImGui handles **rendering only**; touch is read raw via `sceTouchPeek` so the gesture
  engine owns multi-touch. ImGui's own touch input is disabled.
- Backlight control is best-effort: if `sceAVConfigSetDisplayBrightness` fails, the app
  still runs (logged, no crash).

## Open questions / to confirm on hardware

- Touch panel active area and report coordinate range (front panel is typically
  `1920×1088`; verify with `sceTouchGetPanelInfo`).
- Whether dimming to 21 is comfortable enough vs. screen-off for blind use.
- Whether the system dims/restores cleanly on suspend/resume.
- Actual sensitivity values that feel right (tune in Phase 3).
