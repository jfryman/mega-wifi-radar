#!/bin/bash
# The YouTube Short (lode/promo/youtube-short.md). Media lands in out/promo/work/ (git-ignored), never in git.
#   promo/short/short.sh [record|base|voice|sfx|music|fx|mix|thumb|all]...
# voice/sfx/music call ElevenLabs (~/.config/elevenlabs/api_key) and never overwrite a take that exists:
# delete the file in out/promo/work/{vo,sfx,music}/ to get a new take.
set -euo pipefail
P=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$P/../.." && pwd)
W=${PROMO_WORK:-$ROOT/out/promo/work}
mkdir -p "$W"
export P ROOT W

venv() {
    [ -x "$W/venv/bin/python" ] || { python3 -m venv "$W/venv" && "$W/venv/bin/pip" install -q pillow numpy; }
}
trim() {   # trim leading/trailing silence, 48 kHz mono
    ffmpeg -v error -y -i "$1" -af "silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse" -ar 48000 -ac 1 "$2"
}

step() {
    case "$1" in
    record)                       # MAME captures of a clean `make sim` ROM (stale out/sim.bin = wrong traffic)
        [ -f "$ROOT/out/sim.bin" ] || (cd "$ROOT" && make sim)
        "$P/record.sh" ;;
    base) "$P/base.sh" ;;         # segments + captions + end card -> base.mp4 (1080x1920, content shifted 120 px)
    voice)
        python3 "$P/eleven.py" voice
        for f in "$W"/vo/l*.mp3; do n=$(basename "$f" .mp3); [ -f "$W/vo/t$n.wav" ] || trim "$f" "$W/vo/t$n.wav"; done
        [ -f "$W/vo/fine.wav" ] || { trim "$W/vo/fine.mp3" "$W/vo/fine-raw.wav"; ffmpeg -v error -y -i "$W/vo/fine-raw.wav" -af atempo=2.0 "$W/vo/fine.wav"; } ;;
    sfx) python3 "$P/eleven.py" sfx ;;
    music) python3 "$P/eleven.py" music ;;
    fx) venv; (cd "$W" && "$W/venv/bin/python" "$P/fx.py") ;;    # wipes + pop-ups + fine print -> fx/*.png, sfx.txt
    mix) "$P/mix.sh" ;;           # -> mega-wifi-radar-short.mp4
    thumb) venv; (cd "$W" && "$W/venv/bin/python" "$P/thumb.py") ;;
    all) for s in record base voice sfx music fx mix thumb; do step $s; done ;;
    *) echo "short.sh: unknown step $1" >&2; exit 1 ;;
    esac
}
[ $# -gt 0 ] || set -- all
for s in "$@"; do echo "== $s"; step "$s"; done
