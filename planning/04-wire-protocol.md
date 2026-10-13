# 04 — Wire Protocol (VitaTouchPad v2)

The contract between the Vita client and the PC-side server. The server is **out of
scope** for this repo; this document is the spec it must implement.

## Transport

- **UDP**, one datagram per event (small packets, low latency).
- Default port **31415** (configurable on the Vita).
- **Little-endian** fields (ARM native). Structs are packed (`#pragma pack(1)`).
- No handshake required to operate. All state needed to interpret a packet is either in
  the header (`type`) or implied by packet type. `HELLO`/`PING` are optional conveniences.

## Common header (32 bytes, every packet)

| Offset | Size | Field         | Notes |
|-------:|-----:|---------------|-------|
| 0      | 4    | `magic`       | ASCII `"VTPD"` (0x56 0x54 0x50 0x44) |
| 4      | 1    | `version`     | `2` (v1 used `1`; v1 packets are rejected) |
| 5      | 1    | `type`        | event type (below) |
| 6      | 2    | `flags`       | reserved, 0 |
| 8      | 4    | `seq`         | monotonic per session (wraps); for duplicate/reorder rejection |
| 12     | 4    | `timestampMs` | Vita uptime in ms; for latency stats |
| 16     | 16   | `passcode`    | NUL-padded ASCII; server compares to its configured value |

> The server **must** discard datagrams whose `magic`/`version` are wrong or whose
> `passcode` does not match, and **must** ignore unknown `type` values (forward compat).

## Event types

| `type` | Name     | Payload | Meaning |
|-------:|----------|---------|---------|
| 0x01   | `HELLO`  | `u8 mode`, `u8 flags` | Client came online / config changed. `flags` bit0 = `startInScrollMode`. |
| 0x02   | `MOVE`   | `int16 dx`, `int16 dy` | Relative cursor motion (one finger / stick). |
| 0x03   | `FMOVE`  | `u8 fingers`, `int16 dx`, `int16 dy` | Neutral N-finger motion. Carries NO meaning — the server maps it (see below). Replaces v1 `SCROLL`. |
| 0x04   | `BUTTON` | `u8 button`, `u8 action` | Button state change. |
| 0x05   | `MODE`   | `u8 mode` | Touchpad mode changed (cursor/scroll). |
| 0x06   | `PING`   | — | Optional keepalive. Server may ignore or use for presence. |
| 0x07   | `VPAD`   | `u8 button`, `u8 action` | Vita gamepad button edge, for PC shortcut mapping. |
| 0x06   | `PING`   | — | Optional keepalive. Server may ignore or use for presence. |
| 0x07   | `VPAD`   | `u8 button`, `u8 action` | Vita gamepad button edge (press/release), for PC shortcut mapping. |
| 0x08   | `SWIPE`  | `u8 fingers`, `u8 direction` | Discrete directional swipe (mac-style), one packet per gesture. |
| 0x09   | `ZOOM`   | `u8 fingers`, `int16 dSpread` | Pinch spread stream, per frame (`>0` = spreading apart). |
| 0x0A   | `END`    | `u8 ended_type`, `u8 fingers` | Motion-bout terminator: the `FMOVE`/`ZOOM` stream identified just ended. |

### Constants

```
BUTTON_LEFT   = 1
BUTTON_RIGHT  = 2
BUTTON_MIDDLE = 3

ACTION_UP     = 0
ACTION_DOWN   = 1

MODE_CURSOR   = 0
MODE_SCROLL   = 1

SWIPE directions (dominant axis at recognition, 4-way only):
SWIPE_UP=1  SWIPE_DOWN=2  SWIPE_LEFT=3  SWIPE_RIGHT=4

VPAD buttons (Vita controls; SELECT/START are local-only and never sent):
VITA_CROSS=1  VITA_CIRCLE=2  VITA_SQUARE=3  VITA_TRIANGLE=4
VITA_L1=5     VITA_R1=6
VITA_DPAD_UP=7  VITA_DPAD_DOWN=8  VITA_DPAD_LEFT=9  VITA_DPAD_RIGHT=10
VPAD reuses ACTION_UP=0 / ACTION_DOWN=1.
```

Datagram sizes: header 32 + payload (MOVE 36, FMOVE 37, BUTTON 34, MODE 33,
HELLO 34, PING 32, VPAD 34, SWIPE 34, ZOOM 35, END 34).

## C definitions (client side)

```c
#pragma pack(push, 1)
typedef struct {
    char     magic[4];      // "VTPD"
    uint8_t  version;       // 2
    uint8_t  type;
    uint16_t flags;
    uint32_t seq;
    uint32_t timestampMs;
    char     passcode[16];
} VtpHeader;                 // 32 bytes

typedef struct { int16_t dx, dy; }            VtpMove;   // type 0x02, 4B body
typedef struct { uint8_t fingers; int16_t dx, dy; } VtpFMove;   // type 0x03, 5B body
typedef struct { uint8_t button, action; }    VtpButton; // type 0x04, 2B body
typedef struct { uint8_t mode; }              VtpMode;   // type 0x05, 1B body
typedef struct { uint8_t mode, flags; }       VtpHello;  // type 0x01, 2B body
typedef struct { uint8_t button, action; }    VtpVPad;   // type 0x07, 2B body
typedef struct { uint8_t fingers, direction; } VtpSwipe; // type 0x08, 2B body
typedef struct { uint8_t fingers; int16_t dSpread; } VtpZoom; // type 0x09, 3B body
typedef struct { uint8_t ended_type, fingers; } VtpEnd;   // type 0x0A, 2B body
#pragma pack(pop)
```

All structs packed, little-endian (ARM native). Total datagram = 32 + payload.

## Semantics the server must implement

- **MOVE** → inject relative pointer motion `(dx, dy)`.
- **FMOVE** → neutral N-finger motion. The server maps it; the recommended defaults
  (see `05-input-mapping.md`) are: `fingers=1` → scroll (one-finger drag in scroll
  mode), `fingers=2` → scroll, `fingers=3` → unassigned. Sign convention:
  `dy > 0` is **down**, `dx > 0` is **right**; accumulate toward wheel notches
  (≈40 units/notch, remainder carried) or invert per a "natural scroll" setting.
  The name carries no meaning — scrolling is a default mapping, not the wire.
- **SWIPE** → discrete shortcut trigger: `fingers` (3 in the v2 gesture set) +
  `direction` (UP/DOWN/LEFT/RIGHT). Fire the mapped hotkey once. Never moves the cursor.
- **ZOOM** → accumulate `dSpread` (`>0` = spreading apart); emit zoom in/out steps
  at a server-defined spread threshold.
- **END** → terminates the motion bout named in the payload (`ended_type` =
  `FMOVE` or `ZOOM`, plus its finger count). Emitted when a bout ends (finger
  lift, count change, screen leave, shutdown) **iff that bout actually streamed
  packets** — taps and undecided bouts send no `END`. Best-effort like all UDP:
  the normal pattern is exact (stream → `END`), but keep a generous backstop
  timeout (~2 s of silence) for a dropped `END`. Never use the timeout as the
  primary release mechanism — a paused-but-held bout sends no packets *and* no
  `END`, which is exactly how hold-still differs from lift.
- **BUTTON** → set/clear the named mouse button (`DOWN`/`UP`). Both a left-click and a
  drag are expressed as `BUTTON LEFT DOWN` … (`MOVE` …) … `BUTTON LEFT UP`.
- **VPAD** → edge event for a Vita gamepad button, same `DOWN`/`UP` semantics as
  `BUTTON` but driving **PC shortcut mapping** instead of the mouse. The server keeps
  held-state per VPAD button (for chords and auto-repeat) and should treat `UP`
  idempotently (the client re-sends `UP` for held buttons when leaving the touchpad
  screen or shutting down, so a duplicate `UP` for an unheld button is normal).
  `VPAD` is only emitted while the touchpad screen is active — never from Settings.
- **MODE** → advisory; lets the server show an OSD or track state. Not required to
  interpret motion (the client already labels each event: `MOVE` vs `FMOVE`).
- **HELLO** → optional; update presence/last-seen.
- **PING** → optional presence keepalive.

Because the client emits **explicit event types**, the server is effectively stateless for
input injection; only `BUTTON` needs to remember which buttons are held.

## Reference server (pseudocode)

```python
import socket, struct
MAGIC = b"VTPD"; PORT = 31415; PASSCODE = b"secret"
HDR = struct.Struct("<4sBBHII16s")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", PORT))
while True:
    data, addr = sock.recvfrom(256)
    if len(data) < HDR.size: continue
    magic, ver, typ, flags, seq, ts, pc = HDR.unpack_from(data, 0)
    if magic != MAGIC or ver != 2: continue
    if pc.rstrip(b"\x00") != PASSCODE: continue       # auth
    body = data[HDR.size:]
    if typ == 0x02:                                   # MOVE
        dx, dy = struct.unpack_from("<hh", body); mouse_move(dx, dy)
    elif typ == 0x03:                                 # FMOVE (neutral motion)
        fingers, dx, dy = struct.unpack_from("<Bhh", body)
        route_fmove(fingers, dx, dy)                  # default: 1/2->scroll
    elif typ == 0x04:                                 # BUTTON
        btn, act = struct.unpack_from("<BB", body); mouse_button(btn, act)
    elif typ == 0x05:                                 # MODE
        set_mode(body[0])
    elif typ == 0x07:                                 # VPAD
        btn, act = struct.unpack_from("<BB", body); shortcut(btn, act)
    elif typ == 0x08:                                 # SWIPE
        fingers, direction = struct.unpack_from("<BB", body); swipe_action(fingers, direction)
    elif typ == 0x09:                                 # ZOOM
        fingers, dsp = struct.unpack_from("<Bh", body); zoom_accumulate(fingers, dsp)
    elif typ == 0x0A:                                 # END
        ended, fingers = struct.unpack_from("<BB", body); stream_ended(ended, fingers)
    # 0x01 HELLO / 0x06 PING: presence only
```

(`mouse_move`/`mouse_scroll`/`mouse_button` are OS-specific — e.g. `uinput`/`evdev` on
Linux, `SendInput` on Windows, `pynput`, `pyautogui`, etc.)

## Compatibility rules

- v1 → v2 is a deliberate break (`SCROLL` removed, version bumped) made pre-release
  while both ends are ours. Servers reject unknown versions rather than misparse.
- Going forward: bump `version` for any breaking header/layout change.
- New event types may be added; servers must ignore types they don't know.
- `flags` and any reserved bytes stay zero in v2.
