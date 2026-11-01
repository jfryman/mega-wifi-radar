# Plan: cockpit mode (0.3.0)

**Status:** working on the cart, streaming included (2026-11-01, live aircraft near KBNA): solid colours,
16-22 fps, smooth, no visible stutter. One `/v1/terrain` on entry, then `/v1/strip` as the aircraft moves (a
climbing departure at ~330 kt and 682 m cells: one strip in its first 30 s, exactly when it was 8 cells off
centre), tracks every 2 s. The patch's 3 s lead put the aircraft at cell 64.0, 64.2. Left: the manual and the
release. The design, as built, is in [../ui/cockpit.md](../ui/cockpit.md); this file holds what's left and
the speed spike's measurements.

## Left before 0.3.0
1. **Manual + docs:** a cockpit page in `print/manual/`, `docs/` done; credit the elevation sources (AWS Terrain
   Tiles: SRTM, 3DEP, GMTED2010, ETOPO1...) in the manual and docs as their terms ask.
2. **Release 0.3.0** (contract v3: relay and cart together).

## Done on the cart
Byte writes to the VDP duplicate the byte (solid colours). 16-22 fps. A whole patch takes ~2.5 s (~13 KB/s); strips
stream without visible stutter. Tracks every 2.0 s. The radar is intact after a round trip.

## Next improvements (after 0.3.0, not committed)
- **Land cover colours:** rivers, lakes and towns aren't in elevation data, so Nashville reads as plain green. OSM
  water / landuse polygons rasterised by the relay into palette 4-5 and 14 (licence: ODbL, attribution).
  Satellite imagery (Sentinel-2 cloudless) only after a licence check.
- **Low altitude:** below ~1,000 ft cells are 30-60 m and the view is blocky; consider a slightly-behind camera.
- Far terrain shimmers with coarse depth steps (cheap fix: dither the step offset per frame).

## Decisions (kept)
- The cart renders; the relay only sends data. Streaming rendered frames can't be real-time over the UART
  (≤ 125 KB/s raw, ~35 KB a screen).
- Voxel Space; bank as a per-column horizon shear; pitch moves the horizon.
- Only the selected aircraft while in the mode; the relay is stateless (the patch key comes back as `p=`).
- RAM: the mode overlay, not more heap ([../build/ram-budget.md](../build/ram-budget.md)).
- True vertical scale (level = cell / 16), so flat country looks flat.

## Speed spike (`spike/voxel/`, MAME, NTSC)
`make spike-voxel-run` / `-flash`: procedural terrain (`tools/voxelgen`), an autopilot circle, the HUD shows fps and
render time in **scanlines** (262 a frame). A: autopilot, B: depth detail, D-pad: fly. It's a standalone ROM; the
product's renderer (`src/vox.s`) started from it, then put clipping back (real banks are steep).

| Version | Render | FPS |
|---|---|---|
| 160 cols in C, RAM frame buffer + 4-chunk DMA | 28 vb + 5 vb DMA | 1.8 |
| 80 cols, direct VRAM, address per span (C) | 10 vb | 5.4 |
| + precomputed column commands, `z_min` skip | 7 vb | 7.5 |
| + assembly inner loop, `divu` | 5 vb | 10.0 |
| + far-field pairs, 1/4 step past 24 cells, byte writes, HUD every 8th frame | 3 vb | 14.5 |
| + 32-level clamped row table (no clip), unrolled span writes | 3 vb | 15.0 |
| + unrolled tops, `add.w` row offset, asm row table, accumulated roll, HUD in the slack | ~750 lines | 18.4-19.2 |
| + pairs from 6 cells, 1/4 steps from 16 cells, `sub`/`subx` row table, HUD without `sprintf` | 677-756 lines | **20.0** |

Lessons: the per-sample maths dominated, not the VDP (`-DNO_DRAW` in the spike's `vox.s`: 6 of 8 vblanks without
drawing); a depth step's setup + row table + call costs ~1,500 cycles; one `sprintf` HUD line can cost a frame.
The product renderer is a little slower than the spike's best (clipping, per-step C row table, the HUD's
`sprintf`): ~14-16 fps in the emulator. Headroom if needed: the spike's unclipped tops and asm row table (bank
permitting), the whole depth loop in one asm call (~40 lines), not idling to the vblank before the flip, a
64-column view inside window pillars.

Related: [roadmap.md](roadmap.md), [../ui/cockpit.md](../ui/cockpit.md), [../relay/relay.md](../relay/relay.md).
