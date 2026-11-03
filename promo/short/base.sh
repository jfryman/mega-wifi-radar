#!/bin/bash
# The Short's picture: the game at 3x (nearest) in a framed window on the box art's black grid, captions over
# it, cut from $W/rec/ into segments, then shifted 120 px down to sit centred in 9:16 -> $W/base.mp4.
# Segment times are emulated seconds in the recordings; changing a length moves every later cue
# (fx.py TRANS/POPS, cues.txt), see lode/promo/youtube-short.md.
set -euo pipefail
: "${ROOT:?run via short.sh}" "${W:?}"
PX=$ROOT/print/fonts/PressStart2P-Regular.ttf
BK=$ROOT/print/fonts/BarlowCondensed-BlackItalic.ttf
cd "$W"; rm -rf seg; mkdir -p ov seg
RED='#d7141a'; GRN='#46ff6a'; AMB='#ffcc33'; CYN='#5fe8ff'; SIL='#c9ccd1'
ENC=(-c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p -r 60 -c:a aac -b:a 192k -ar 48000 -ac 2)

# Background: 40 px grid, red-ruled header, silver/red frame round the 960x672 game window at (60,560).
magick -size 40x40 xc:'#0b0b0c' -fill '#1a1a1e' -draw 'rectangle 0,0 39,1' -draw 'rectangle 0,0 1,39' ov/tile.png
magick -size 1080x1920 tile:ov/tile.png ov/grid.png
magick ov/grid.png \
  -fill "$RED" -stroke none -draw "rectangle 0,150 1080,154" -draw "rectangle 0,236 1080,240" \
  -fill '#0b0b0c' -draw "rectangle 0,155 1080,235" \
  -font "$BK" -pointsize 76 -fill white -gravity north -annotate +0+152 'MEGA WIFI RADAR' \
  -gravity northwest -fill "$SIL" -draw "rectangle 48,548 1032,1244" \
  -fill "$RED" -draw "rectangle 54,554 1026,1238" -fill black -draw "rectangle 58,558 1022,1234" \
  -font "$PX" -pointsize 22 -fill "$SIL" -gravity north -annotate +0+1290 'SEGA GENESIS / MEGA DRIVE' \
  -annotate +0+1326 '+ MEGAWIFI CARTRIDGE' \
  ov/bg.png

cap() {   # cap name "LINE1" "LINE2" "SUB" [subcolour]
  magick -size 1080x1920 xc:none -gravity north -font "$PX" \
    -fill white -pointsize 52 -annotate +0+330 "$2" \
    -fill "$GRN" -pointsize 52 -annotate +0+410 "$3" \
    -fill "${5:-$AMB}" -pointsize 30 -annotate +0+492 "$4" \
    ov/$1.png
}
cap boot   "MY SEGA GENESIS" "HAS WI-FI NOW"      "(YES, REALLY)" "$CYN"
cap radar  "IT TRACKS REAL"  "PLANES. LIVE."      "ADS-B + NEXRAD WEATHER"
cap notice "AND YELLS WHEN"  "ONE FLIES OVER"     "MY HOUSE (WITH A CHIME)"
cap popup  "PICK ANY PLANE"  "..."                "REG / TYPE / OPERATOR"
cap cock   "...AND RIDE ALONG" "IN 3D"            "REAL TERRAIN ON A 68000"
cap over   "LOOK OUT THE"    "WINDOW!"            "LIVE COCKPIT VIEW"
cap themes "9 RADAR THEMES"  "STARS TO GAMEBOY"   "PICK YOUR SCOPE"
cap log    "KEEPS A LOGBOOK" "ALL DAY LONG"       "FASTEST / HIGHEST / RAREST"

# The emulator build's boot says EMULATOR BUILD / RECORDED FEED (game rows 52-68): cover it with a banner.
magick -size 1080x1920 xc:none -fill "$RED" -draw "rectangle 60,712 1020,764" \
  -font "$PX" -pointsize 26 -fill white -gravity north -annotate +0+725 'LIVE AIR TRAFFIC SCOPE' ov/bootmask.png

seg() {   # seg out rec start dur caption [extra overlay]
  local out=$1 src=rec/$2 ss=$3 t=$4 c=$5 extra=${6:-}
  local inputs=(-loop 1 -i ov/bg.png -ss "$ss" -t "$t" -i "$src.avi" -ss "$ss" -t "$t" -i "$src.wav" -loop 1 -i ov/$c.png)
  local fc="[1:v]scale=960:672:flags=neighbor,fps=60[g];[0:v][g]overlay=60:560:shortest=1[a];[a][3:v]overlay=0:0[b]"
  if [ -n "$extra" ]; then inputs+=(-loop 1 -i ov/$extra.png); fc="$fc;[b][4:v]overlay=0:0[v]"; else fc="$fc;[b]null[v]"; fi
  ffmpeg -v error -y "${inputs[@]}" -filter_complex "$fc" -map '[v]' -map 2:a -t "$t" "${ENC[@]}" seg/$out.mp4
}

#        out rec     start dur  caption           output time
seg 01 boot     9.2  5.2 boot bootmask   #  0.0  cleared for takeoff, lift-off
seg 02 radar   24.0  4.6 radar           #  5.2  busy scope + weather
seg 03 radar   36.6  5.4 notice          #  9.8  LOOK UP! box; its chime at 11.9-14.0
seg 04 cockpit 19.4  1.4 popup           # 15.2  detail pop-up
seg 05 cockpit 22.0  3.6 cock            # 16.6  cockpit ride
seg 06 cockpit 38.6  3.0 over            # 20.2  OVERHEAD! ticker
seg 07 themes  22.6  1.1 themes          # 23.2  STARS (record.sh: theme k on from 22.3+4k s)
seg 08 themes  26.6  1.1 themes          #       AMBER
seg 09 themes  30.6  1.2 themes          #       NIGHT
seg 10 themes  34.6  1.1 themes          #       MARINE
seg 11 themes  42.6  0.5 themes          #       POCKET
seg 12 themes  46.6  0.5 themes          #       SUNSET
seg 13 themes  50.6  0.6 themes          #       ERAM
seg 14 logbook 22.0  2.6 log             # 29.3  logbook page

# End card (31.9-49.2): title + call to action over the dimmed radar, held for the fine print.
magick -size 1080x1920 xc:none -gravity north \
  -fill '#0b0b0ce0' -draw "rectangle 0,300 1080,1500" \
  -font "$BK" -pointsize 150 -fill white -annotate +0+380 'MEGA WIFI' \
  -fill "$RED" -pointsize 230 -annotate +0+520 'RADAR' \
  -font "$PX" -pointsize 30 -fill "$GRN" -annotate +0+840 'REAL AIRCRAFT. REAL TIME.' \
  -annotate +0+890 'ON YOUR GENESIS.' \
  -fill "$RED" -draw "rectangle 140,990 940,1150" -fill white -pointsize 34 -annotate +0+1020 'FREE + OPEN SOURCE' \
  -pointsize 22 -annotate +0+1090 'GITHUB.COM/JFRYMAN/MEGA-WIFI-RADAR' \
  -fill "$SIL" -pointsize 24 -annotate +0+1220 'BUILD YOUR OWN CART' \
  ov/end.png
ffmpeg -v error -y -loop 1 -i ov/bg.png -ss 21 -t 17.3 -i rec/radar.avi -ss 21 -t 17.3 -i rec/radar.wav -loop 1 -i ov/end.png \
  -filter_complex "[1:v]scale=960:672:flags=neighbor,fps=60[g];[0:v][g]overlay=60:560:shortest=1[a];[3:v]fade=in:st=0:d=0.4:alpha=1[e];[a][e]overlay=0:0[v];[2:a]afade=out:st=15.3:d=2.0[au]" \
  -map '[v]' -map '[au]' -t 17.3 "${ENC[@]}" seg/15.mp4

(cd seg && ls *.mp4) | sed 's/^/file /' > seg/list.txt
ffmpeg -v error -y -f concat -safe 0 -i seg/list.txt -c copy seg/joined.mp4
# Shift down 120 px (3 grid cells, so the grid lines up) to centre the content in 9:16.
ffmpeg -v error -y -loop 1 -i ov/grid.png -i seg/joined.mp4 \
  -filter_complex "[0:v][1:v]overlay=0:120:shortest=1,format=yuv420p[v]" -map '[v]' -map 1:a \
  -c:v libx264 -preset slow -crf 14 -r 60 -c:a copy -movflags +faststart base.mp4
ffprobe -v error -show_entries format=duration -of csv=p=0 base.mp4
