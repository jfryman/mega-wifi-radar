# Cockpit mode (`src/cockpit.c`, `src/voxel.c`, `src/vox.s`; relay `terrain.go`, `track.go`, `dem.go`)

The view from one aircraft over real terrain, in real time. The cart draws it (Voxel Space, straight into VRAM);
the relay only sends data: a **terrain patch** centred on the aircraft now and then, and the aircraft's **track**
placed in that patch every 2 s. Entry: **A in the detail pop-up**, or **Y** (6-button). **B** goes back to the radar.

```mermaid
sequenceDiagram
  participant M as main.c
  participant C as cockpit.c
  participant R as relay
  M->>M: popup_close, radar_stop (no V-Int may touch the VDP)
  M->>C: cockpit_run(icao)
  C->>R: GET /v1/terrain?icao (32,872 B, streamed into the overlay)
  C->>R: GET /v1/track?icao&p=key
  loop every frame
    C->>C: advance camera (velocity + blended correction), render, HUD line, flip
    C-->>R: every 120 frames: /v1/track; drifted 12 cells: /v1/terrain again
  end
  C->>M: B (or lost 20 s)
  M->>M: radar_up (radar_init refills the overlay), poll, reselect the aircraft
```

## Data (contract v3, `src/arframe.h`; [../architecture/contract.md](../architecture/contract.md))
- **`/v1/terrain?icao=&v=`** → `struct ar_terrain` (104 B) + 128×128 height levels (0..63) + 128×128 palette
  indices (4..15), rows from the south edge, west to east. 404 if the aircraft isn't in the feed.
  - `cell_m` = height above ground / 5 (30..3000 m): the camera sits ~5 cells up at any altitude.
  - `level_cm` = cell / 16 (true vertical scale), or more if the relief won't fit 64 levels.
  - `base_m` = 1st percentile of the ground (one bad pixel can't sink it); `hmax` = highest level.
  - `pal[16]` = PAL0: 1-3 sky, 4-5 water, 6-8 low ground, 9-11 mid, 12-13 high, 14 urban (unused), 15 snow.
  - `key` = `"lat,lon,cell,level,base"`: the cart passes it back verbatim; the relay is stateless.
  - `AR_T_FLAT`: no elevation data (offline): flat ground, the mode still works.
- **`/v1/track?icao=&p=<key>&v=`** → `struct ar_track` (40 B): `x, y` (1/256 cell from the SW corner), `z` (1/256
  level), `vx, vy, vz` per second, `heading`, `bank` (deg), `age` (s since the position), plus `alt`, `gs`,
  `vrate`, `cs` for the HUD. No `AR_K_FOUND`: the relay doesn't know it.

Relay side ([../relay/relay.md](../relay/relay.md)): elevation from AWS Terrain Tiles (Terrarium PNG, no key,
decoded with `image/png`, 48 tiles kept), zoom picked so a pixel ≤ a cell, bilinear samples at cell centres. Sea is
elevation < -5 m (bathymetry); colours are height bands of the patch's own relief, shaded from the north-west.
Bank: reported `roll`, else from `track_rate`, else from the relay's previous sighting (`turns`), clamped ±45°.
Track lookups use the cached snapshot if < 1.5 s old, else `/api/v2/hex/<hex>` (rate-limited 1/s).

## RAM: the mode overlay ([../build/ram-budget.md](../build/ram-budget.md))
`struct vox_ram` (34,624 B) sits in `overlay_ram`, the block the radar uses for its scope tiles, trail bitmap and
track table (36,736 B). Static RAM doesn't grow with the mode; leaving it, `radar_init` refills the overlay.
```c
struct vox_ram {
    u8 h[AR_TN * AR_TN];        /* level, converted in place to (level / 2) * 2: a byte offset into ytab */
    u8 c[AR_TN * AR_TN];        /* palette index, converted to c * 0x11: the byte written to VRAM */
    struct vox_col cols[2][VOX_COLS];   /* per VRAM copy: top, roll, row-0 VDP command */
    s16 prev_top[2][VOX_COLS];  /* sky delta */
    s16 ytab[VOX_LEVELS];
    u16 zt[VOX_NZ_MAX];
};
```
**Invariant:** the maps must always be converted. A failed download leaves them part raw; `voxel_patch`'s
conversion is idempotent (`& 0x3E`, `(c & 0xF) * 0x11`), so `load_terrain` runs it whatever happens. A raw odd
height would make `vox.s` read a word at an odd address: an address error.

## Renderer (`voxel.c` + `vox.s`)
80 logical columns × 112 rows (screen rows 16-127), each logical pixel 4 px wide. Tiles are column-major, so a
column's rows are 4 bytes apart: with the VDP auto-increment at 4, a vertical span is one address command and n
**byte** writes (a byte write puts the byte on both bus halves: four identical pixels). No frame buffer, no DMA.
- Two VRAM copies: plane A maps copy 0 on rows 2-15 and copy 1 on 18-31; `voxel_flip` waits for a vblank and sets
  plane A's vertical scroll to 0 or 128. HUD: window plane from row 16.
- No tile column straddles a 16 KB VRAM boundary (a column that would starts past it), so a span's command is
  `cmd + (y << 18)`.
- Depth: 0.75..64 cells (beyond, the map wraps); steps × (1 + 1/8), × (1 + 1/4) past 16 cells; one sample per
  two columns past 6 cells (`vox_pairs`); steps start at `z_min`, where the tallest terrain reaches the bottom row.
- Projection: rows per level at 1 cell `k = 160 · level / cell` (160 px focal length, a 90° view), Q8, ≤ 64.
- Bank: each column's roll = `tan(bank) · 4` rows per column from the centre (right bank lifts the right); the
  asm clips rows above the screen. Pitch moves the horizon: `160 · vrate / (gs · 101)` rows, ±30.
- Sky: fixed bands (palette 1-3); only rows a copy's terrain uncovered since its last frame are repainted.
- `vox.s` per column: sample (index, height, row) ~84 cycles, test ~30, span ~95 + 8 per row (jump into 112
  unrolled writes). Measured in the spike: [../plans/cockpit-mode.md](../plans/cockpit-mode.md).

## Camera (`cockpit.c`)
- Position 16.16 cells, height 16.16 levels; velocity per frame from the fix. Each frame: `pos += v`, plus
  `err / 32` for 32 frames after a fix (`err` = fix + v · age − pos); errors > 6 cells (or 32 levels) snap.
- Heading eases 1/8 per frame toward the fix's heading, which advances at the rate seen between fixes. Bank and
  horizon step 1 per frame toward their targets.
- New patch when the camera is > 12 cells from the centre **and** > 12 cells from where the last patch was asked
  for (a pack or relay that can't move it would otherwise reload every frame), or its height halves / doubles.
- All requests block (the view pauses: ~0.1-0.5 s a track, ~0.6 s+ a patch on the relay side); the camera then
  catches up by elapsed time. No good fix for 20 s: `SIGNAL LOST`, back to the radar.

## HUD (window rows 16-27)
Row 17 callsign (PAL2 cyan) + status (`LIVE`, amber `NO SIGNAL` / `TERRAIN...` / `SIGNAL LOST`); 19 altitude, ground
speed; 21 vertical speed, heading, bank; 23 cell size, fps; 26 `B  BACK TO RADAR`. One line per frame. On return,
the radar selects the same aircraft once the first poll has it.

## Emulator
`relay -pack-cockpit` (`make fake-feed`) writes `res/fake/kbna_cockpit.bin`: a 750 m-cell patch around KBNA from
real elevation (needs the network) and every recorded aircraft's track in it. The fake `feed_track` plays the
recording in real time, looping. `SIM_PRESS="10:P1 Down,11:P1 C,13:P1 A"` enters it in `make sim-run`.

Related: [detail-popup.md](detail-popup.md), [screen-layout.md](screen-layout.md), [../net/feed.md](../net/feed.md).
