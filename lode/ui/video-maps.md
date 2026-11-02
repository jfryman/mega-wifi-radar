# Video maps (`src/vmap.c`, `scope.c`, `radar.c`; relay `maps.go`, `runways.csv`)

Faint lines on the scope like a TRACON's video map: the **runways** of every known airport in view (solid, 2 px), and, at ranges
≤ 25 nm, the **home airport's extended centrelines** (dashed, 10 nm from each threshold, a tick every mile; every 5 nm
at 25 nm). Setup MAP ON/OFF (default ON). Owner's decisions 2026-11-02: runways + centrelines only (rivers, roads,
airspace later), their own colour, centrelines off at 50/100 nm, shipped alone as contract v6.

```mermaid
flowchart LR
  OA[OurAirports runways.csv<br/>tools/runwaygen → relay/runways.csv, embedded] --> B[buildMap: project like the frame,<br/>clip to the disc, scope pixels at the range]
  B -->|GET /v1/map: ar_map + ≤160 x ar_seg u8 x1 y1 x2 y2| C[vmap_check: fetch into the renderer's buffer<br/>on radar start + after each zoom]
  C --> V[V-Int: ≤ 96 px of lines a frame<br/>scope_map_line → fixed pixels in C_MAP]
  V --> S[sweep slow path skips fixed pixels]
```

## Relay (`maps.go`)
- `runways.csv`: 1,684 runways at the cart's 1,142 home airports (large, scheduled service; closed runways and ends
  without positions dropped): `airport,le_ident,le_lat,le_lon,he_ident,he_lat,he_lon`. Regenerate:
  `cd tools && go run ./runwaygen -o ../relay/runways.csv`. `//go:embed`, parsed at start.
- `buildMap(lat, lon, range)`: positions → nm east/north (`nmXY`, the frame's azimuthal equidistant projection) →
  pixels `104 + x·104/range`, `104 − y·104/range`. Order (the 160 cap cuts the end first): every runway whose midpoint
  is within range + 2 nm, nearest first (kept even as a 1-px dot); then, if range ≤ 25 and a runway midpoint lies
  within 5 nm of home (the home airport), each end's centreline (from the threshold away from the other end, 10 nm);
  then the ticks (perpendicular, ±2 px). `clipCircle` clips to radius 103 about (104, 104); degenerate centrelines and
  ticks are dropped. No segments → `AR_M_NONE`.
- KBNA: 5 nm 42 segments, 10 nm 82 (4 runways, 8 centrelines, 70 ticks: far ones fall off the disc), 25 nm 28, 50/100
  nm 4 (no other airport from the list within 100 nm).
- `GET /v1/map?lat=&lon=&range=&v=6` → `ar_map` (10 B: "AM", ver, mflags, u16 range_nm, u8 runways, centres, ticks,
  rsv) + that many `ar_seg` (4 B) in class order. The classes give the cart its line styles (the first v6 layout had
  one u16 count; changed before release, so still v6). No cache (it's cheap). `-pack-map OUT -lat -lon` writes `res/fake/maps.bin` ("FM", u16 n, per range u16 len +
  body, padded even).

## Cart
- `vmap.c` (main loop, after `wx_check` in the poll): fetches when the range on screen isn't the one fetched
  (`vmap_reset` in `radar_up`; zooms). The body goes **straight into the renderer's buffer**: `radar_map_begin()` stops
  drawing (and asks for a clear if lines were drawn) and lends `map_buf` (648 B); `feed_map(buf, size)` fills it;
  `radar_map_end(ok)` starts drawing. One copy, because RAM is tight (below). A failed fetch waits 60 s. MAP_OFF:
  begin + end(FALSE) clears it.
- `radar_rescale()` (main, ints off) wipes the weather cells and the map list at once, so a fetch right after a zoom
  can't be wiped by a rescale still pending in the V-Int (this fixed the same race for weather). The V-Int's rescale
  then clears fixed pixels (`scope_clear_dots`) and restarts the raster at segment 0.
- V-Int, after `wx_rows`: whole lines until ≥ `MAP_PX_PER_FRAME` (96) pixels; a full 10 nm map takes ~10-20 frames.
  Styles by index (`map_runways`, `map_centres` from the header): runways `MAP_THICK` (a second pixel across the line:
  y + 1 for shallow lines, x + 1 for steep), centrelines `MAP_DASHED` (3 on, 3 off, `MAP_DASH_ON/OFF`), ticks plain.
- **Why the styles** (owner, 2026-11-02): at 25 nm a runway is 5 px of one long line and read as part of its
  centreline ("both runways are on the edges of the 10 nm ring"); STARS draws centrelines dashed and runways solid.
  Everything is true scale: KBNA's runways come out within 15 ft of their published lengths.
  `req_mapclear` (MAP off, a replaced map): `scope_clear_dots()` and every track's dots redrawn (`dirty = 2`).

## Fixed pixels (`scope.c`, [sweep.md](sweep.md))
`trail_bits` (1 bit/pixel, in the overlay) now means **"fixed pixel: the sweep leaves it alone"**, for trail dots and
map pixels alike; `trail_cnt[tile] > 0` sends a tile to the slow path, which **skips** fixed pixels on draw and retire
(trail dots used to be overdrawn and restored; now they stay visible under the line, like rings). Who owns a pixel is
its colour: `map_px` sets the bit and `C_MAP` (never over a ring pixel of the ROM tiles); `scope_dot` leaves a pixel that
is set and `C_MAP` alone (on and off), so a dot never erases a map line. No second bitmap: that would cost 4.5 KB.

```c
for (u16 k = 0; k < ne + no; k++) {         /* paint(): slow path */
    s16 off = *d++;
    u16 bit = off * 2 + (k >= ne);
    if (trail_bits[bit >> 3] & bitmask[bit & 7]) continue;   /* fixed: dot or map */
    base[off] = (k >= ne) ? ((base[off] & 0xF0) | v) : ((base[off] & 0x0F) | (v << 4));
}
```

## Colour: PAL0 11 (`C_MAP`)
PAL0 was full (13 weather), so the sweep's glow slots went from 10 to 9 (2..10; tail 24 steps, slots repeat every 27:
still retired before reuse); each theme's `glow` lost a near-duplicate shade. Map colours (0x0BGR): Classic `0x444`,
STARS `0x666`, AMBER `0x246`, NIGHT `0x024`, MARINE `0x644`, ICE `0x666`, POCKET `0x046`, SUNSET `0x846`, ERAM `0x666`.

## Measured (MAME, `RADAR_DEBUG`, Classic 10 nm KBNA, weather + map)
V-Int average 157-164 of 262 lines with the map, 161-174 without (the notice box and sweep position dominate the
noise); 0 late frames. The slow path over map tiles is not a problem at this density.

## Budget
Cart start-up heap **8,463 B** (min 8,192): `map_buf` 650 B. The next RAM user must free some first (candidates:
`popup_text`/`grid`/`label_cells`, 784 B each; the overlay has no spare: the radar is its larger user).

Related: [weather.md](weather.md), [sweep.md](sweep.md), [radar-renderer.md](radar-renderer.md),
[../relay/relay.md](../relay/relay.md), [../plans/watch-arrivals.md](../plans/watch-arrivals.md).
