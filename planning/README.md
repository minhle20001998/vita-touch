# VitaTouchPad — Planning & Progress

A native **PS Vita** app (VitaSDK + Vita2d-ImGui) that turns the Vita into a **wireless
trackpad / remote input device** for a PC. Touch gestures are sampled on the Vita and
broadcast as **compact binary UDP packets** to a PC-side server, which injects the
corresponding mouse/scroll events.

- Transport: **UDP** to `<server_ip>:<server_port>` (default port **31415**)
- Auth: **plain passcode** stored as a fixed 16-byte field in every packet
- This repo builds **only the Vita client**. The PC server is assumed to exist; its
  contract is the wire protocol in [04-wire-protocol.md](04-wire-protocol.md).

## Product summary

- **Main screen = touchpad mode.** The Vita screen is treated as a relative trackpad:
  drag one finger → move the PC cursor; **tap** → left click; **tap-hold-drag** → hold
  left button and drag; **two-finger drag** → scroll; **two-finger tap** → right click.
- **Battery saver:** on entering touchpad mode the backlight is **dimmed to the floor**
  (default) or **turned fully off** (optional). Touch input stays active. Brightness is
  restored when leaving touchpad mode (entering Settings) or exiting the app.
- **START** toggles **scroll mode**: one-finger dragging scrolls instead of moving the
  cursor. Toggling also informs the PC server via a `MODE` packet. A small on-screen
  indicator shows the current mode.
- **SELECT** opens the **Settings** screen (restores backlight): server IP, port,
  passcode (via the system on-screen keyboard), sensitivity, battery mode, and a
  connection/test indicator.

## Stack

| Layer        | Choice                              | Status |
|--------------|-------------------------------------|--------|
| Language     | C++ (ImGui requires it)             | have   |
| Build        | CMake + VitaSDK                     | have   |
| UI           | Vita2d-ImGui (SDK `imgui_vita2d`)   | have   |
| Font         | Literata TTF, Latin                 | to add |
| Net (UDP)    | SceNet (`sceNetSocket`/`sceNetSendto`) | have |
| Touch        | SceTouch (`sceTouchPeek`)           | have   |
| Backlight    | SceAVConfig (`sceAVConfigSetDisplayBrightness`) | have |
| Keyboard     | SceImeDialog (system IME)           | have   |

## Screen map

```
Touchpad mode (960x544, backlight dimmed/off)      Settings (960x544, backlight full)
┌────────────────────────────────────────────┐     ┌──────────────────────────────────┐
│  ▪ VITA TOUCHPAD            [CURSOR]        │     │  Server IP   [192.168.1.100 ]    │
│                                             │     │  Port        [31415         ]    │
│              (whole screen is              │     │  Passcode    [••••••        ]    │
│               the touch surface)            │     │  Sensitivity [====----] 1.0      │
│                                             │     │  Battery     ( ) Dim  ( ) Off    │
│  ○ connected · server 192.168.1.100:31415   │     │  [Test / Save]  [Back]           │
└────────────────────────────────────────────┘     └──────────────────────────────────┘
   START = scroll mode toggle                         SELECT closes → returns to touchpad
   SELECT = settings
```

## Progress tracker

| Phase | Name | Status | VPK tested |
|-------|------|--------|------------|
| 0 | Project skeleton (C++/ImGui/Literata/build) | done (v1, verified on hardware) | ✅ |
| 1 | Foundation: config, UDP + protocol, backlight, settings | done (v3, verified on hardware + Vita3K) | ✅ |
| 2 | Touchpad core: gestures → events (cursor mode) | done (v5–v8, single-finger on Vita3K, two-finger on hardware) | ✅ |
| 3 | Scroll mode, battery polish, tuning | in progress (v10 built, HW test pending) | — |
| 4 | Packaging, LiveArea assets, final hardware pass | in progress (v12 built, HW pass pending) | — |

**Testing loop:** every phase ends with a **Build & test (VPK delivery)** section — the
phase isn't complete until a `.vpk` is built, installed on hardware, and signed off by the
user. Build: `cmake -S .. -B build && cmake --build build` → `build/VitaTouchPad.vpk`.

Detailed phase breakdown and checkboxes: [03-phases.md](03-phases.md).

## Docs index

- [01-requirements.md](01-requirements.md) — confirmed UX, gestures, controls, decisions & assumptions
- [02-architecture.md](02-architecture.md) — module layout, networking, gesture engine, backlight, config
- [03-phases.md](03-phases.md) — implementation roadmap with checkboxes
- [04-wire-protocol.md](04-wire-protocol.md) — the UDP packet contract + reference server pseudocode
- [05-input-mapping.md](05-input-mapping.md) — gesture → wire → PC action mapping (what each input does)
- [../pc-server-requirement.md](../pc-server-requirement.md) — requirements for the PC logging receiver

## How to update

Edit the relevant doc; keep the **Progress tracker** table in this file in sync with phase
statuses.
