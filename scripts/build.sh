#!/bin/sh
# Build VitaTouchPad and produce incrementally-versioned VPKs in build/vpk/.
#
# Usage: ./scripts/build.sh
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

mkdir -p "$BUILD_DIR"

echo "== configuring =="
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"

echo "== building =="
cmake --build "$BUILD_DIR"

echo "== done =="
ls -t "$BUILD_DIR"/vpk/VitaTouchPad_v*.vpk 2>/dev/null | head -1
