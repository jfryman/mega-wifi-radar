#!/bin/bash
# The Short's final cut: base.mp4 + the fx/ overlay, and the audio built from its parts ->
# $W/mega-wifi-radar-short.mp4. Audio: game sound (lifted inside the chime window), the announcer
# (cues.txt), SFX (sfx.txt from fx.py), music (its build-up under line 1 so the beat lands on "WI-FI!";
# bass-heavy; ducked a little under the voice; dipped for the chime), the fast fine print over a low bed
# of the groove. Two passes: measure loudness, then a fixed gain to -14 LUFS and a limiter (a fixed gain
# keeps the quiet fine print quiet; single-pass loudnorm would pump it up).
set -euo pipefail
: "${P:?run via short.sh}" "${W:?}"
cd "$W"
MUS=${MUSIC:-music/m1.mp3}; OUT=${OUT:-mega-wifi-radar-short.mp4}
MV=${MUSIC_VOL:-1.15}; GV=${GAME_VOL:-0.9}; DR=${DUCK_RATIO:-1.8}
MUSIC_SKIP=${MUSIC_SKIP:-0.36}                     # track A kicks in at 3.0 s: lands on "WI-FI!" (2.64 s)
CHIME_A=${CHIME_A:-11.8}; CHIME_B=${CHIME_B:-13.8} # the LOOK UP chime plays alone in here
BED_AT=${BED_AT:-38.0}; BED_VOL=${BED_VOL:-0.10}   # the groove again, ~14 dB under the fine print

VOCH="highpass=f=90,acompressor=threshold=-24dB:ratio=8:attack=3:release=60:makeup=8,equalizer=f=180:t=q:w=1:g=2,equalizer=f=3200:t=q:w=1.2:g=5,asoftclip=type=tanh,volume=1.3"
IN=(-i base.mp4 -framerate 30 -i fx/%05d.png -i "$MUS"); F=""; V=""; X=""; i=3
while IFS='|' read -r at file from to fx; do
  case "$at" in ''|'#'*) continue ;; esac
  ms=$(python3 -c "print(int($at*1000))"); IN+=(-i "vo/$file")
  tr="atrim=start=${from}"; [ -n "$to" ] && tr="${tr}:end=${to}"
  case "$fx" in
    echo) ch="$VOCH,apad=pad_dur=2,aecho=0.8:0.7:200|400|600:0.5|0.32|0.18" ;;
    big)  ch="$VOCH,aecho=0.8:0.6:180|360:0.35|0.2" ;;
    fine) ch="highpass=f=150,lowpass=f=7000,acompressor=threshold=-24dB:ratio=4:makeup=4,volume=0.6" ;;
    *)    ch="$VOCH" ;;
  esac
  F="${F}[${i}:a]aresample=48000,${tr},asetpts=PTS-STARTPTS,${ch},adelay=${ms}|${ms},apad[v${i}];"; V="${V}[v${i}]"; i=$((i+1))
done < "$P/cues.txt"
NV=$((i-3)); j=$i
while IFS='|' read -r t n g; do
  ms=$(python3 -c "print(max(0,int($t*1000)))"); IN+=(-i "sfx/$n.mp3")
  F="${F}[${i}:a]aresample=48000,aformat=channel_layouts=stereo,volume=${g},adelay=${ms}|${ms},apad[s${i}];"; X="${X}[s${i}]"; i=$((i+1))
done < sfx.txt
NS=$((i-j))

BED_MS=$(python3 -c "print(int($BED_AT*1000))")
DIP="1-0.9*clip((t-$CHIME_A)/0.15,0,1)*clip(($CHIME_B-t)/0.25,0,1)"        # ~0.1 inside the chime window
GAMEUP="1+12*clip((t-$CHIME_A)/0.1,0,1)*clip(($CHIME_B-t)/0.25,0,1)"       # the recorded chime is quiet
FC="${F}\
${V}amix=inputs=${NV}:normalize=0,pan=stereo|c0=c0|c1=c0,asplit=3[vo][sc1][sc2];\
${X}amix=inputs=${NS}:normalize=0,volume=0.45,volume='${DIP}':eval=frame[sx];\
[0:a]volume=${GV},volume='${GAMEUP}':eval=frame[g];[g][sc1]sidechaincompress=threshold=0.02:ratio=8:attack=20:release=400[gd];\
[2:a]aresample=48000,asplit[mu][mb];\
[mb]atrim=start=4:end=15.3,asetpts=PTS-STARTPTS,bass=g=6:f=85:w=0.7,equalizer=f=3000:t=q:w=1.5:g=-6,volume=${BED_VOL},afade=in:d=1.2,afade=out:st=9.8:d=1.5,adelay=${BED_MS}|${BED_MS},apad[bed];\
[mu]atrim=start=${MUSIC_SKIP},asetpts=PTS-STARTPTS,bass=g=9:f=85:w=0.7,equalizer=f=45:t=q:w=1:g=3,equalizer=f=3000:t=q:w=1.5:g=-3,\
acompressor=threshold=-16dB:ratio=3:attack=8:release=120:makeup=2,asoftclip=type=tanh,volume=${MV},\
volume='${DIP}':eval=frame[m];[m][sc2]sidechaincompress=threshold=0.05:ratio=${DR}:attack=10:release=250[md];\
[gd][md][bed][vo][sx]amix=inputs=5:normalize=0:duration=first"

LUFS=$(ffmpeg -hide_banner -nostats "${IN[@]}" -filter_complex "${FC},ebur128=framelog=quiet[a]" -map '[a]' -f null - 2>&1 \
  | awk '/Integrated loudness/{f=1} f&&/I:/{print $2; exit}')
GAIN=$(python3 -c "print(round(-14 - ($LUFS), 2))")
echo "mix: ${LUFS} LUFS -> ${GAIN} dB"
ffmpeg -v error -y "${IN[@]}" -filter_complex \
 "[0:v][1:v]overlay=0:0:eof_action=pass:format=auto,format=yuv420p[v];${FC},volume=${GAIN}dB,alimiter=limit=0.89:level=false,aresample=48000[a]" \
 -map '[v]' -map '[a]' -c:v libx264 -preset slow -crf 15 -r 60 -c:a aac -b:a 192k -movflags +faststart "$OUT"
ls -la "$OUT"
