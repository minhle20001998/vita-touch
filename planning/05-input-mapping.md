# 05 — Input Mapping (gesture → wire → PC action)

The single source of truth for what each Vita input **does** on the PC. Read this
alongside [04-wire-protocol.md](04-wire-protocol.md) (byte layout) when implementing
or debugging the PC server.

Definitions: **gesture** = what the finger does; **wire** = packet(s) the Vita emits;
**PC action** = what the server must inject into the OS.

## Coordinate and sign conventions

- All motion is **relative**. Deltas are in Vita panel units scaled by the user's
  sensitivity settings; the server applies them **verbatim** (no server-side scaling).
- `dx > 0` = right, `dy > 0` = **down** (panel origin is top-left, y grows downward).
- The Vita reports raw finger displacement. Interpretation policy lives server-side:
  - `MOVE`: apply `(dx, dy)` to the cursor position directly.
  - `FMOVE`: neutral motion — map by finger count using the table below. The wire
    never says "scroll"; scrolling is a *default server mapping*, re-mappable without
    touching the Vita. The recommended scroll mapping: accumulate deltas, emit one
    wheel notch per **40 accumulated units** (remainder carries over; threshold is
    server-tunable). Positive `dy` accumulates toward **scroll-down** (classic desktop
    wheel direction). If the server wants "natural" (finger-follows-content)
    scrolling, it inverts the sign — that choice is a server setting, never the
    client's.
  - `ZOOM`: accumulate `dSpread`; emit zoom in/out steps at a server-defined spread
    threshold (`>0` = spreading apart = zoom in).

## One finger (unchanged by v2)

| # | Gesture | Condition | Wire packets (in order) | PC action |
|---|---------|-----------|-------------------------|-----------|
| G1 | One-finger drag | moves > tap slop (default 12 units) | cursor mode: `MOVE(dx,dy)` per frame; scroll mode: `FMOVE(1,dx,dy)` per frame | move cursor / scroll (server maps `FMOVE(1)` → scroll by default) |
| G2 | Tap (left click) | released within slop, before hold time (default 250 ms), no second finger | `BUTTON LEFT DOWN`, `BUTTON LEFT UP`, nothing between | left click |
| G3 | Tap-hold-drag | held ≥ hold time without moving, then moves | `BUTTON LEFT DOWN`, then `MOVE(dx,dy)` per frame, then `BUTTON LEFT UP` on release | press-and-hold left, drag, release |
| G6 | START button | pressed on touchpad screen | `MODE(new_mode)` + local mode flip | advisory only (see below); one-finger drag + stick meaning change per mode table |
| G7 | SELECT / Circle | — | **no packets** (local screen navigation only) | none |

## Two fingers (the scroll/pinch hand)

| Gesture | Wire event | Default PC action |
|---|---|---|
| 2-finger move (any speed, any direction) | `FMOVE` stream, `fingers=2, dx, dy` per frame (centroid motion) | scroll — vertical and horizontal |
| 2-finger tap | `BUTTON RIGHT DOWN` → `BUTTON RIGHT UP`, nothing between | right click |
| 2-finger pinch/spread | `ZOOM` stream, `fingers=2, dSpread` per frame (positive = spreading apart) | zoom in/out |

Two-finger left/right slides are horizontal scroll (first row) — no special-casing.

## Three fingers (the shortcut hand)

| Gesture | Wire event | Default PC action |
|---|---|---|
| 3-finger swipe up | `SWIPE`, single packet on release, `fingers=3, dir=UP` | Mission Control / task view |
| 3-finger swipe down | `SWIPE`, `fingers=3, dir=DOWN` | App Exposé / show desktop |
| 3-finger swipe left | `SWIPE`, `fingers=3, dir=LEFT` | switch space/app left |
| 3-finger swipe right | `SWIPE`, `fingers=3, dir=RIGHT` | switch space/app right |
| 3-finger tap | `BUTTON MIDDLE DOWN` → `BUTTON MIDDLE UP` | middle click (macOS calls this slot "Look up" — mappable) |
| 3-finger slow move (below swipe distance) | `FMOVE` stream, `fingers=3` | unassigned (macOS uses this shape for window-dragging; slot reserved) |

All "unassigned" rows ship as server no-ops with config slots — mappable without
touching the Vita. Only the scroll/tap/click defaults are live on day one.

Notes (apply to all contact counts):

- A click is **defined** as a `DOWN` immediately followed by an `UP` for the same
  button with no motion between them. A drag is a `DOWN`, then motion, then `UP`.
  The server does not need gesture memory beyond held-button state.
- 4+ simultaneous contacts collapse to the three-finger rows (the `fingers` field
  still reports the real count for future use).
- If a finger lands **after** a hold-drag already started, the Vita aborts the drag
  first: `BUTTON LEFT UP`, then the multi-finger bout begins. The server will
  observe a complete click-like pair followed by the new gesture — apply both as
  received, do not coalesce.
- Contact juggling never *spuriously* clicks: count changes re-anchor without
  emitting and disarm the plain release-click. But 3-finger intent survives
  staggered lifts — real hands never lift 3→0 atomically, so displacement
  accumulates session-wide from the 3-finger landing through step-down tails
  (3→2→1): a swipe still fires at full release if the session traveled past
  `swipeDist`. Tap candidacy follows the session rule below (motionless +
  press-free), so slow clean placements tap while post-drag placements stay
  silent.
- **Merged pair (panel reports one blob for two close contacts):** a confirmed
  2→1 step-down classifies by position — within `min(spacing/3, 25)` of the old
  centroid it stays TWO and keeps streaming `FMOVE(2)` from the blob (packets keep
  `fingers=2`); near an old finger position it's a genuine lift (disarmed ONE +
  tap inheritance as before). Zoom evaluation freezes while merged (spread is
  unmeasurable) and resumes on split-back with no re-anchor jump. A close-finger
  scroll therefore never teleports the cursor *or* starves the release-click.
- **Swipe = distance + dominant direction at release, any speed.** A 3-finger bout
  traveling past `swipeDist` (default 100 units, Settings-tunable 40–400) along one
  axis emits exactly one `SWIPE`; below it the bout is stream-only. Motion during
  an undecided two-finger landing window carries into the triple vote, so an
  already-moving swipe doesn't lose its first stretch when the third finger lands
  late. Slow 3-finger drags still trigger — there is no velocity test anywhere.
- **Pinch vs scroll lock per bout:** per-finger motion is projected onto the
  inter-finger axis fixed at bout entry — only spreading-apart/squeezing along
  the connecting line counts as zoom (parallel drifting scores ~zero in any
  direction, so diagonal pinches pass and diagonal scrolls never zoom). Zoom
  additionally needs 3 consecutive radially-dominant frames past `pinchSlop`
  (default 40, tunable 16–120), so grip-settling blips can't lock; scroll keeps
  its single-crossing lock (suppressed while a radial streak is active) and wins
  exact ties. The loser never emits for that bout. **Pinch additionally needs a
  clean two-finger landing**: after a 3→2 step-down the remaining pair is usually
  still spreading from the swipe motion, so it may scroll-stream but can never
  lock zoom — otherwise every swipe tail would zoom on the way out.
- **Touchdown hold (anti-teleport).** Fresh-touchdown cursor motion is buffered
  for `landingHoldMs` (default 80, tunable 0–200, `0` disables) instead of streamed:
  a second finger landing inside the window discards the buffer (close landing →
  clean two-finger start, zero cursor jump); otherwise it flushes as one catch-up
  `MOVE` with bit-exact totals. Slop, locks, taps, and drag timing all run on live
  positions underneath, so honesty is unaffected — only emission is deferred.
- **Per-frame jump cap with flick continuity (120).** An over-cap single-frame
  centroid delta is absorbed (re-anchor, no emit, no accrual) only when it is an
  *isolated* teleport: the previous frame was slow/at rest, the direction
  reversed, or the magnitude is wildly larger (`>5×` the cap). An over-cap delta
  that continues a fast, same-direction streak is real motion and passes through
  untouched — so flicks and flings stream every frame instead of stalling, while
  fusion-jitter teleports still die on contact.

## Mode table (cursor vs scroll)

`MODE_CURSOR` is the default. One-finger drag **and the right stick** change wire
type with mode; everything else is identical in both modes. The server applies each
packet by its own type and must never reinterpret motion from the last `MODE`.

| Gesture | Cursor mode | Scroll mode |
|---------|-------------|-------------|
| One-finger drag (G1) | `MOVE` | `FMOVE(1,…)` → scroll by default |
| Right stick | `MOVE` (cursor drive) | `FMOVE(1,…)` (continuous scroll drive) |
| Tap (G2) | left click | left click (unchanged) |
| Tap-hold-drag (G3) | left drag | hold does nothing special; motion streams as `FMOVE(1,…)` |
| Two-finger move | `FMOVE(2,…)` → scroll | `FMOVE(2,…)` → scroll (unchanged) |
| Two-finger tap | right click | right click (unchanged) |
| Two-finger pinch | `ZOOM` stream | `ZOOM` stream (unchanged) |
| 3-finger swipe/tap/move | `SWIPE` / middle-click / `FMOVE(3,…)` | unchanged (mode-independent) |

`MODE` packets are **advisory** (OSD display, server-side logging). Because the client
already labels every motion packet (`MOVE` vs `FMOVE`), the server **must not**
reinterpret motion based on the last seen `MODE` — apply each packet by its own type.

## Button and action codes (repeated here so this doc stands alone)

- Buttons: `LEFT = 1`, `RIGHT = 2`, `MIDDLE = 3` (middle = 3-finger tap).
- Actions: `UP = 0`, `DOWN = 1`.
- A well-formed stream never sends `UP` for a button that isn't held, and every
  `DOWN` is eventually followed by `UP` (the client triple-sends `UP` on screen
  leave/shutdown; it also sends `UP` immediately when a drag is aborted). If the
  server ever believes a button is stuck, timing out held state after ~5 s of no
  packets from that sender is an acceptable recovery.

## Ordering and duplicates

- Packets carry a per-session monotonic `seq`. UDP may reorder or duplicate: the
  server should apply packets in arrival order and may use `seq` to drop obvious
  duplicates for logging. It must **not** hold motion packets waiting for gaps —
  latency matters more than strict ordering for a pointing device.
- `HELLO` (boot, settings change) and `PING` (keepalive) carry no input; they update
  presence/last-seen only and must never move the cursor or change button state.

## Stateful mappings (press/release without timeouts)

Any mapping that holds OS state across a bout (window-drag on `FMOVE(3)`, press-type
zoom, etc.) keys off the `END` terminator instead of silence:

- First stream packet of a bout → press/acquire (e.g. mouse down for window-drag).
- `FMOVE`/`ZOOM` packets → move/accumulate while held.
- `END{ended_type, fingers}` → release (e.g. mouse up). The payload names the exact
  bout that ended, so no context tracking is needed to parse it.
- Taps never emit `END` (their `BUTTON UP` already terminates); undecided bouts
  (lifted before any motion streamed) and bout switches (2→1, 2→3, screen leave,
  shutdown) terminate the old bout with `END` first — every streamed bout is closed.
- `END` is best-effort UDP like everything else: keep a generous backstop silence
  timeout (~2 s) for a dropped terminator, but never release on short silence —
  a held-still finger sends nothing *and* no `END`, which is precisely how
  hold-still differs from lift. A pause mid-drag must not drop the drag.

## Vita buttons (PC shortcuts)

While the touchpad screen is active, every mapped Vita control emits `VPAD`
**edge events**: one packet on press (`DOWN`), one on release (`UP`). No auto-repeat
from the client — if the server wants held-button repeat (e.g. held D-pad-Up =
volume ramping), it implements repeat itself from held state.

| Vita control | VPAD code | Notes / suggested mapping (server-configurable) |
|--------------|-----------|--------------------------------------------------|
| Cross | 1 | confirm / Enter (server's choice) |
| Circle | 2 | back / Escape (server's choice) |
| Square | 3 | unassigned |
| Triangle | 4 | unassigned |
| L1 | 5 | e.g. Alt-Tab, prev tab |
| R1 | 6 | e.g. next tab |
| D-pad Up/Down/Left/Right | 7/8/9/10 | e.g. volume, media prev/next |
| SELECT | — | **local-only**: opens/closes Settings, never sent |
| START | — | **local-only**: scroll-mode toggle (+ `MODE` packet), never sent as VPAD |

Rules:

- `VPAD` is emitted **only on the touchpad screen** — never from Settings or while
  the system keyboard is open.
- Leaving the touchpad screen (or app shutdown) re-sends `UP` for every held mapped
  button, so a duplicate `UP` for an already-released button is normal traffic —
  the server applies `UP` idempotently.
- Chords work naturally: hold L1 (`VPAD L1 DOWN`), tap R1 (`DOWN`+`UP`), release L1
  (`UP`). The server tracks held set from the edge stream.

## Right stick (cursor drive)

The right analog stick moves the cursor **without touching the screen**: tilt direction
sets the movement direction, speed is constant at `stickSpeed` (default 600 panel
units/s, configurable 120–2400) regardless of tilt amount past the deadzone
(radius 24 of 0–255 travel; inside it nothing is emitted).

- Wire: plain `MOVE(dx,dy)` per frame in cursor mode, `FMOVE(1,dx,dy)` in scroll
  mode (normalized direction × speed/60, fractional remainder carried across
  frames so low speeds stay smooth). No new packet type.
- Follows the touchpad mode like one-finger drag: cursor drive normally, continuous
  scroll drive (tilt-and-hold) in scroll mode. The on-screen feed shows `STICK` vs
  `STICK FMOVE1` accordingly.
- Independent of touch gestures and `VPAD` buttons; centering the stick stops motion
  instantly, so no release/stuck handling is needed.
- Not scaled by the finger `sensitivity` setting — stick speed is its own knob.

## Worked example

User double-taps a folder and drags it (cursor mode, passcode `secret`):

1. finger down/up quickly → `BUTTON LEFT DOWN`, `BUTTON LEFT UP` (click selects)
2. finger down, hold 300 ms → `BUTTON LEFT DOWN` (drag arm)
3. finger moves (+40, +10) → `MOVE(40,10)` … (window/folder follows)
4. finger up → `BUTTON LEFT UP` (drop)

Server-side that is: click, press-left, move-cursor, release-left. No gesture
recognition required on the server — only button-state memory.
