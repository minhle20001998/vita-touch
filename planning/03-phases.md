# 03 — Phases / Roadmap (detailed)

Status key: `[ ]` pending · `[x]` done. The Progress tracker table in
[README.md](README.md) mirrors phase statuses.

**Cross-cutting conventions used by every phase:**
- All rendering, ImGui, input, and UDP sends happen on the **main thread**. No worker
  thread is required (UDP is non-blocking).
- Touch is read raw via `sceTouchPeek`; ImGui's own touch input is disabled.
- Every screen implements: draw, input, enter/leave lifecycle hooks.
- Backlight is always restored on app exit (RAII-style save/restore).
- Settings persist immediately on change (atomic write).

**Build & test loop (every phase):** each phase ends with a **Build & test (VPK delivery)**
section — the phase is not complete until a working `.vpk` is produced, installed, and
tested on hardware by the user. Build command:
`cmake -S .. -B build && cmake --build build` (output: `build/VitaTouchPad.vpk`).
Install via VitaShell (USB/FTP); open; run the checklist below.

---

## Phase 0 — Project skeleton

**Goal:** buildable C++ Vita app: Vita2d-ImGui renders Literata; main loop, screen manager,
and packaging pipeline work.
**Depends on:** nothing.

### 0.1 Toolchain & CMake
- [x] Copy the starter pattern from `vita-reader/test-app` (CMakeLists, `vitasdk.cmake`,
      `scripts/version_vpk.sh`, `assets/livearea`)
- [x] Set `VITA_TITLE_ID "VTPAD001"`, `VITA_APP_TITLE "VitaTouchPad"`
- [x] Configure with `-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake`
- [x] Link stubs: `SceDisplay`, `SceGxm`, `SceCtrl`, `SceTouch`, `SceNet`, `SceNetCtl`,
      `SceAVConfig`, `SceSysmodule`, `ScePgf`, `SceCommonDialog`, `SceAppUtil`,
      `SceAppMgr`, `SceKernelThreadMgr`, `SceIofilemgr`, plus `vita2d`, `freetype`,
      `png16`, `jpeg`, `z`, `m`
- [x] Keep the POST_BUILD packaging pipeline (param.sfo → elf → fself → vpk) working

### 0.2 Rendering, loop, screens
- [x] `vita2d_init` → ImGui context → fonts → `ImGui_ImplVita2D_Init`
- [x] Main loop: `NewFrame` → draw → `RenderDrawData` → `swap_buffers`; vblank wait on
- [x] Disable ImGui touch (`ImGui_ImplVita2D_TouchUsage(false)`); enable gamepad nav
- [x] Screen manager stub: `enum AppScreen { Touchpad, Settings }`
- [x] Fonts: bundle Literata; readable UI scale on 960×544

### 0.3 Input sanity
- [x] `sceCtrlSetSamplingMode(ANALOG_WIDE)`, `sceTouchSetSamplingState(FRONT, START)`
- [x] Verify START / SELECT / Circle / D-pad read each frame
- [x] Verify `sceTouchPeek` returns reports; log panel info (`sceTouchGetPanelInfo`)
- [x] Boot log helper (`ux0:data/VitaTouchPad/boot.log`)

### 0.4 Verify
- [x] VPK installs and runs on hardware; no font/UI artifacts
- [x] Buttons and raw touch reports confirmed

**Acceptance:** `.vpk` boots, draws ImGui UI with Literata, reads buttons + raw touch,
packages cleanly.

### Build & test (VPK delivery)
- [x] Build → `build/VitaTouchPad.vpk`; install via VitaShell
- [x] Boots to a placeholder touchpad screen with readable text
- [x] START/SELECT/Circle/D-pad respond; touch reports logged
- [x] Report issues → fix, rebuild, re-verify

---

## Phase 1 — Foundation

**Goal:** persistent config, working UDP send layer implementing the protocol, backlight
control, and a functional Settings screen with IME entry.
**Depends on:** Phase 0.

### 1.1 Storage & config
- [x] `config.cpp`: load/save `ux0:data/VitaTouchPad/config.json` (atomic temp+rename)
- [x] Schema per [02-architecture.md](02-architecture.md): serverIp, serverPort, passcode,
      sensitivity, scrollSensitivity, batteryMode, dimLevel, startInScrollMode
- [x] First run seeds defaults (empty IP → "not configured")

### 1.2 UDP + protocol
- [x] `protocol.h`: packed header/payload structs + type/button/mode constants
- [x] `udp.cpp`: `sceSysmoduleLoadModule(NET)`, `sceNetInit`, socket, destination resolve
      (`sceNetInetPton`), `send()` per datagram, `shutdown()`
- [x] Build packets with magic "VTPD", version, type, seq, timestampMs, passcode
- [x] Send `HELLO` when config loaded/changed
- [x] Send-error counter → connection indicator state

### 1.3 Backlight
- [x] `screen.cpp`: read max brightness; `apply(full|dim|off)`; `save()`/`restore()`
- [x] Dim floor = 21; off = 0; failures logged, non-fatal
- [x] Enter/leave Settings toggles full ↔ battery mode

### 1.4 Settings screen + IME
- [x] Reuse `ime.cpp` (system keyboard) for IP / port / passcode fields
- [x] Form with gamepad navigation; validate IP/port; Save persists immediately
- [x] Battery mode selector (Dim / Off); sensitivity slider/step
- [x] "Test" action: send a `HELLO` and report send result on screen
- [x] Circle leaves Settings → back to touchpad (re-apply battery mode)

### 1.5 Verify
- [x] Enter config, restart app → values persist
- [x] `HELLO`/test packet visible to a scratch listener on the PC (e.g. `nc -ul 31415`)
- [x] Brightness dims on touchpad, restores in Settings and on exit

**Acceptance:** config persists; the app emits well-formed protocol packets to the
configured server; backlight control works; Settings is usable end-to-end.

### Build & test (VPK delivery)
- [x] Build → new `build/VitaTouchPad.vpk`
- [x] Set IP/port/passcode via IME; confirm persistence across reboot
- [x] Verify packets with a PC listener (correct magic/type/passcode)
- [x] Dim/off/full transitions verified on hardware
- [x] Report findings → fix, rebuild, re-verify

---

## Phase 2 — Touchpad core (cursor mode)

**Goal:** full one- and two-finger gesture detection emitting correct protocol events,
with relative cursor movement, in cursor mode.
**Depends on:** Phase 1.

### 2.1 Touch sampling
- [x] `touchpad.cpp`: per-frame `sceTouchPeek(FRONT)`; track active finger count/ids
- [x] Convert panel motion to deltas; apply `sensitivity`; clamp to `int16_t`
- [x] Handle 0→1→2 transitions (abort one-finger gesture when a second finger lands)

### 2.2 One-finger gesture
- [x] Move: stream `MOVE` deltas once past `TAP_SLOP`
- [x] Tap: release within `TAP_SLOP` and `HOLD_MS` → left down+up
- [x] Tap-hold-drag: hold ≥ `HOLD_MS` → left down, stream `MOVE`, left up on release

### 2.3 Two-finger gesture
- [x] Motion → `SCROLL` (axis lock optional); apply `scrollSensitivity`
- [x] Two-finger tap (short, minimal motion) → right down+up

### 2.4 UI
- [x] `ui/touchpad.cpp`: mode label, connection indicator, "not configured" hint
- [x] Dim overlay respects current backlight mode

### 2.5 Verify (with a PC listener that logs + a real server if available)
- [x] Cursor moves smoothly; direction/sign correct (fix inversions) — Vita3K
- [x] Tap = left click; drag selects/moves windows — Vita3K
- [x] Two-finger scroll works — hardware (accepted as "kinda works"; feel tuned in Phase 3)
- [x] Right click fires; no stuck buttons after fast multi-touch transitions — hardware

**Acceptance:** the Vita behaves as a usable relative trackpad; packets match the spec.

### Build & test (VPK delivery)
- [x] Build → new `build/VitaTouchPad.vpk`
- [ ] On hardware: move, tap, drag, scroll, right-click all correct
- [ ] Tune `sensitivity`/`tapSlop`/`holdMs`; re-verify
- [ ] Report findings → fix, rebuild, re-verify

---

## Phase 3 — Scroll mode, battery polish, tuning

**Goal:** START scroll-mode toggle (local + `MODE` packet), battery mode refinements,
and hardware-tuned feel.
**Depends on:** Phase 2.

### 3.1 Scroll mode
- [x] START toggles cursor ↔ scroll; send `MODE`; persist `startInScrollMode`
- [x] In scroll mode, one-finger drag emits `SCROLL`; tap still left-clicks
- [x] Clear on-screen mode indicator (color/label)

### 3.2 Battery & lifecycle
- [x] Dim vs Off applied correctly; leave/enter Settings restores appropriately
- [x] Handle suspend/resume: battery mode re-asserted every ~5 s (no power callbacks
      needed; also covers any system interference)
- [ ] Optional: brief brightness bump on significant input (deferred — only if
      dim-only proves hard to use on hardware)

### 3.3 Tuning
- [x] Sensitivity/acceleration feel; scroll speed; thresholds via Settings
      (sensitivity, scroll sens, tap slop 4–64, hold time 100–1000 ms)
- [x] Stable 60 fps (vblank-paced); UDP sends are fire-and-forget, no per-frame allocs
- [x] Panel coordinate range confirmed on hardware: active/display (0,0)-(1919,1087)

### 3.4 Verify
- [ ] Toggle scroll mode mid-session; PC server reflects `MODE` (if it displays it)
- [ ] Long session stays stable; no button stuck; brightness never left off by accident
- [ ] WiFi drop → disconnected indicator → reconnect restores sending

**Acceptance:** two-mode touchpad with correct battery behaviour, tuned and stable.

### Build & test (VPK delivery)
- [x] Build → new `build/VitaTouchPad.vpk`
- [ ] START toggle works; scroll mode scrolls; indicator accurate
- [ ] Dim/off behaviour and Settings restore confirmed; suspend/resume checked
- [ ] Report findings → fix, rebuild, re-verify

---

## Phase 4 — Packaging & final pass

**Depends on:** Phase 3.

### 4.1 Assets & packaging
- [x] LiveArea icon0/bg/startup (8-bit palette PNGs via `scripts/gen_livearea.py`), template.xml
- [x] Title id `VTPAD0001` (must stay exactly 9 chars — enforced in CMake), app title "VitaTouchPad"; versioned VPK copies
- [x] On-screen version string (matches `VitaTouchPad_vN.vpk`)

### 4.2 Robustness
- [x] Validate all config inputs (IPv4 dotted-quad, port 1–65535, passcode ≤ 15;
      out-of-range values sanitized on load, verified by 48 host-test checks)
- [x] No leaks over a long session (audit: per-frame paths use static/stack buffers
      only; single heap alloc is the 1 MB net pool, freed on shutdown; single thread,
      so no ImGui/vita2d cross-thread misuse; no async layer, so no retry/backoff
      or duplicate in-flight issues by construction)
- [x] Graceful "not configured" / "disconnected" states everywhere

### 4.3 Final hardware pass
- [ ] Boot → configure → touchpad → gestures → scroll mode → settings → exit
- [ ] Verify brightness restored on exit; no stuck mouse buttons
- [ ] Confirm packets against [04-wire-protocol.md](04-wire-protocol.md)

**Acceptance:** release-quality `.vpk` with accurate docs and a clean hardware pass.

### Build & test (final release)
- [x] Build → final `build/VitaTouchPad.vpk` with LiveArea assets
- [ ] Full user pass on hardware against a real PC server
- [ ] Tag release candidate; address remaining findings

---

## Milestone summary

| Milestone | Delivers |
|-----------|----------|
| M0 (P0) | Bootable UI shell reading input (tested VPK) |
| M1 (P1) | Config + UDP protocol + backlight + Settings + IME (tested VPK) |
| M2 (P2) | Relative trackpad: move/tap/drag/scroll/right-click (tested VPK) |
| M3 (P3) | Scroll mode + battery polish + tuning (tested VPK) |
| M4 (P4) | Release-quality VPK + accurate protocol docs (tested VPK) |
