#!/bin/sh
# Incrementally version the freshly built VPK into an output folder so the
# user can report which build number works/broken on hardware.
#
# Usage: version_vpk.sh <build_dir> [output_dir]
set -e

build_dir="$1"
output_dir="${2:-$build_dir}"
count_file="$build_dir/.vpk_count"

mkdir -p "$output_dir"

n=0
if [ -f "$count_file" ]; then
    n=$(cat "$count_file")
fi
n=$((n + 1))
echo "$n" > "$count_file"

cp "$build_dir/VitaTouchPad.vpk" "$output_dir/VitaTouchPad_v${n}.vpk"
echo "VPK versioned: $output_dir/VitaTouchPad_v${n}.vpk"
