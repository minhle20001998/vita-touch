# PC Server — Requirements (logging receiver)

## Goal

A minimal PC-side UDP server that receives VitaTouchPad input packets, validates them
per the wire protocol, and logs the decoded events to the console. This is a
**diagnostic / milestone tool**: it proves the Vita client and the protocol work
end-to-end before any real mouse-injection server is built.

Wire protocol contract: `planning/04-wire-protocol.md` (v1, binary, little-endian).

## Functional requirements

1. **Listen on UDP**
   - Bind `0.0.0.0:<port>` (default **31415**); port configurable via CLI arg or env var.
   - One datagram = one event; max expected size 36 bytes; oversize datagrams are
     ignored (but logged at debug level).
2. **Validate every datagram, in this order**
   - Minimum length 32 bytes (header size); shorter → drop + count.
   - `magic` must be `"VTPD"`; otherwise → drop + count.
   - `version` must be `2`; otherwise → drop + count.
   - `passcode` field (NUL-padded, 16 bytes) must equal the configured passcode
     (default e.g. `secret`, configurable via CLI arg or env var); otherwise → drop +
     count (log the source address so a mismatched Vita is diagnosable).
   - Unknown `type` values → ignore the body but count (forward compatibility).
3. **Decode and log**
   - `HELLO` → log `HELLO mode=<cursor|scroll> startInScroll=<bool> seq=<n>`.
   - `MOVE` → log `MOVE dx=<n> dy=<n> seq=<n>`.
   - `FMOVE` → log `FMOVE fingers=<n> dx=<n> dy=<n> seq=<n>`.
   - `SWIPE` → log `SWIPE fingers=<n> dir=<up|down|left|right> seq=<n>`.
   - `ZOOM` → log `ZOOM fingers=<n> dspread=<n> seq=<n>`.
   - `END` → log `END <fmove|zoom> fingers=<n> seq=<n>`.
   - `BUTTON` → log `BUTTON <left|right|middle> <down|up> seq=<n>`.
   - `VPAD` → log `VPAD <cross|circle|square|triangle|l1|r1|up|down|left|right> <down|up> seq=<n>`.
   - `MODE` → log `MODE <cursor|scroll> seq=<n>`.
   - `PING` → log at debug level only (or counted silently) to avoid console spam.
   - Every line includes sender `ip:port`, and `timestampMs` may be shown in verbose mode.
4. **Runtime stats**
   - Track per-sender: packets received, dropped (by reason: short/bad-magic/
     bad-version/bad-passcode), last-seen time.
   - Print a summary every N seconds (e.g. 10 s) or on demand (Ctrl+C prints it
     before exit — must exit cleanly).
5. **Configuration**
   - `port` (default 31415), `passcode` (default empty = accept anything? or a fixed
     default — decide explicitly; recommended: require it to be set, fail fast if
     missing, so auth is never accidentally off).
   - `verbose` flag for per-packet hex dumps and PING logging.

## Non-functional requirements

- **Stdlib only** (Python 3: `socket` + `struct`; hard to beat for a logging tool).
  Single file, no dependencies to install.
- Startup time trivial; per-packet cost negligible (target: 60+ packets/s without lag).
- Runs on Windows, Linux, macOS without changes.
- Ctrl+C exits cleanly after printing the final stats summary.

## Out of scope

- Mouse/keyboard injection (that's the follow-up "real" server).
- Handshake, sessions, encryption, replay protection beyond the `seq` field.
- Multi-Vita arbitration, GUI, persistence.

## Acceptance

- [ ] Starts with `server.py --port 31415 --passcode secret`, prints "listening".
- [ ] Vita app → Settings → same IP/port/passcode → **Send test HELLO** → server logs
      `HELLO` with the Vita's address.
- [ ] Dragging/tapping on the Vita (Phase 2+) produces a readable live stream of
      `MOVE`/`FMOVE`/`BUTTON` lines.
- [ ] Wrong passcode on the Vita → packets dropped, counted, source logged; no crash.
- [ ] Ctrl+C prints the stats summary and exits with code 0.
