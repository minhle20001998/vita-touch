#!/usr/bin/env bash
#
# toggle-touchpad.sh - toggle the laptop touchpad on/off.
#
# Toggles the X11 input device(s) behind the touchpad (the "… Touchpad" node
# plus its legacy "… Mouse" node) and mirrors the state to Cinnamon's
# touchpad setting so the desktop stays in sync.
#
# Usage: toggle-touchpad.sh [toggle|on|off|status]
#
set -euo pipefail

usage() {
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
}

action="${1:-toggle}"
case "$action" in
    toggle|on|off|status) ;;
    -h|--help) usage; exit 0 ;;
    *) echo "toggle-touchpad: unknown action '$action'" >&2; usage >&2; exit 2 ;;
esac

# --- locate the touchpad nodes ---------------------------------------------
# e.g. "SYNA8018:00 06CB:CE67 Touchpad" (main node) and
#      "SYNA8018:00 06CB:CE67 Mouse"  (legacy emulation node).
touchpad_ids() {
    # xinput decorates device names with tree glyphs ("⎜   ↳ NAME"), so strip
    # everything up to the first alphanumeric character before matching.
    local names name prefix id primary=""
    names="$(xinput --list --name-only | sed 's/^[^[:alnum:]]*//')" || return 1

    while IFS= read -r name; do
        case "$name" in *Touchpad*) primary="$name"; break ;; esac
    done <<< "$names"

    [[ -n "$primary" ]] || { echo "toggle-touchpad: no touchpad found" >&2; return 1; }

    # The pad also exposes a legacy "<same model>" Mouse node; take both.
    prefix="${primary% Touchpad}"
    while IFS= read -r name; do
        [[ "$name" == "$prefix"* ]] || continue
        id="$(xinput --list --id-only "$name" 2>/dev/null)" && [[ -n "$id" ]] &&
            printf '%s\n' "$id"
    done <<< "$names"
}

mapfile -t IDS < <(touchpad_ids)
((${#IDS[@]})) || exit 1

state=0                                  # 1 = enabled, 0 = disabled
for id in "${IDS[@]}"; do
    # NB: xinput separates the value with a TAB, so take the last field.
    if [[ "$(xinput list-props "$id" 2>/dev/null | awk '/Device Enabled/{print $NF}')" == "1" ]]; then
        state=1
        break
    fi
done

case "$action" in
    status) echo "$([[ $state -eq 1 ]] && echo enabled || echo disabled)"; exit 0 ;;
    on)     target=1 ;;
    off)    target=0 ;;
    toggle) target=$((1 - state)) ;;
esac

apply() {
    local id
    for id in "${IDS[@]}"; do
        xinput set-prop "$id" "Device Enabled" "$1" 2>/dev/null || true
    done
    # Mirror to Cinnamon/GNOME's setting (harmless if the schema is absent).
    gsettings set org.cinnamon.desktop.peripherals.touchpad send-events \
        "$([[ $1 -eq 1 ]] && echo enabled || echo disabled)" 2>/dev/null || true
    gsettings set org.gnome.desktop.peripherals.touchpad send-events \
        "$([[ $1 -eq 1 ]] && echo enabled || echo disabled)" 2>/dev/null || true
}

apply "$target"

if command -v notify-send >/dev/null 2>&1 && [[ -n "${DISPLAY:-}" ]]; then
    notify-send \
        --app-name="Touchpad" \
        --icon=input-touchpad \
        --expire-time=2000 \
        --urgency=low \
        --hint=string:x-canonical-private-synchronous:touchpad-toggle \
        "Touchpad" \
        "$([[ $target -eq 1 ]] && echo 'Enabled' || echo 'Disabled')" \
        2>/dev/null || true
fi

echo "$([[ $target -eq 1 ]] && echo enabled || echo disabled)"
