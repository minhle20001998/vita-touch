#!/bin/sh
# Host-side verification for VitaTouchPad: protocol bytes, gesture engine,
# and config round-trip — all without Vita hardware.
#
# Usage: ./host-test/run.sh   (run from the repo root)
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
T="$ROOT/host-test"

echo "== protocol =="
python3 "$T/check_protocol.py" > "$T/.out_protocol" 2>&1
tail -2 "$T/.out_protocol"
grep -q "^FAIL" "$T/.out_protocol" && { echo "PROTOCOL FAILURES"; exit 1; }

echo "== gestures =="
python3 "$T/check_gesture.py" > "$T/.out_gesture" 2>&1
tail -2 "$T/.out_gesture"
grep -q "^FAIL" "$T/.out_gesture" && { echo "GESTURE FAILURES"; exit 1; }

echo "== config =="
g++ -O2 -Wall -I "$T/stubs" -I "$ROOT/src" \
    "$T/test_config.cpp" "$T/fakefs.cpp" "$ROOT/src/config.cpp" \
    -o "$T/test_config"
"$T/test_config" > "$T/.out_config" 2>&1
tail -2 "$T/.out_config"
grep -q "^FAIL" "$T/.out_config" && { echo "CONFIG FAILURES"; exit 1; }

echo "ALL SUITES PASS"
