#!/bin/bash
# Record the Short's gameplay from out/sim.bin in headless MAME (the mw-bringup-rom image, as scripts/sim):
# $W/rec/<name>.avi + .wav, emulated time from power-on. Presses use sim/snap.lua's SIM_PRESS syntax.
set -euo pipefail
: "${ROOT:?run via short.sh}" "${W:?}"
CTR=${CTR:-$(command -v podman || command -v docker)}
IMG=${SIM_IMAGE:-mw-bringup-rom:fedora42}
mkdir -p "$W/rec"

rec() {   # rec name end_s "presses"
    "$CTR" run --rm --user "$(id -u):$(id -g)" -v "$ROOT:/w" -v "$W/rec:/rec" -w /w -e HOME=/tmp \
        -e SIM_SNAP_AT="$2" -e SIM_PRESS="${3:-}" -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy "$IMG" \
        mame genesis -cart /w/out/sim.bin -noautosave -video none -nothrottle -skip_gameinfo \
        -state_directory /tmp/sta -cfg_directory /tmp/cfg -nvram_directory /tmp/cfg \
        -snapshot_directory /rec -snapsize 320x224 -autoboot_script /w/sim/snap.lua \
        -aviwrite "$1.avi" -wavwrite "/rec/$1.wav" >/dev/null 2>&1 || true
    [ -s "$W/rec/$1.avi" ] || { echo "record: $1.avi missing" >&2; exit 1; }
}

# Setup opens on START, RIGHT changes the theme, START saves: theme k+1 (STARS first) is on the radar
# from 22.3+4k to 24+4k s. The first press waits to 20 s: the boot ends near 17-18 s and a START during
# it is swallowed, which shifted every theme in one run.
TH=""; t=20
for _ in 1 2 3 4 5 6 7 8; do TH="$TH,$t:P1 Start,$((t + 1)):P1 Right,$((t + 2)):P1 Start"; t=$((t + 4)); done

rec boot 18 "" &
rec radar 46 "18:P1 Down,18.4:P1 Down,18.8:P1 Down" &
rec cockpit 44 "18:P1 Down,18.4:P1 Down,18.8:P1 Down,19.2:P1 C,20.5:P1 A" &
rec themes 54 "${TH#,}" &
rec logbook 26 "18:P1 B,21:P1 C" &
wait
for n in boot radar cockpit themes logbook; do [ -s "$W/rec/$n.avi" ] || { echo "record: $n.avi missing" >&2; exit 1; }; done
