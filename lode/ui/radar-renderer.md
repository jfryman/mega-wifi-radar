# Radar renderer (src/radar.c, scope.c, traffic.c)

After `radar_start()` the renderer **owns the VDP and runs entirely in the V-Int**
(`SYS_setVIntCallback`). The main loop never touches the VDP; it only polls the feed
(blocking HTTP is fine, the sweep keeps turning), ingests frames, and handles the pad.

```mermaid
flowchart LR
  subgraph main[main loop]
    P[poll on each north pass] --> I[traffic_ingest: next, tag text, bearing]
    J[pad: zoom/list/tags/sort/theme/pop-up/START] --> R[radar_* requests]
  end
  subgraph vint[V-Int, every frame]
    U[V-Blank: CPU-copy dirty tiles + sprite DMA + glow palette] --> A[scope_advance]
    A --> X[apply tracks whose bearing == sweep step]
    X --> D[<= 2 tag redraws] --> S[build sprite table] --> T[<= 3 panel rows] --> Q[<= 3 pop-up rows] --> B[notice box] --> W[<= 3 weather rows] --> M[<= 96 px of map lines]
  end
  I -. tr_state / tr_brg / track.next .-> X
```

## Contracts
- Shared state: main writes `track.next*`, `tr_state`, `tr_brg` under `SYS_disableInts()`. The
  V-Int owns `cur`, trails, tags and sprites. `radar_panel_pal()` / `radar_popup_row()` copy text and a
  palette per row under ints-off, and skip rows that didn't change.
- The pop-up ([detail-popup.md](detail-popup.md)) is the window plane over cols 0–27 with an opaque font copy
  (`t_ofont`, built by `opaque_font()` in `radar_init`); it appears once all its rows are written and hides the
  sprites. VRAM after the scope: 5 blip tiles, 4 box tiles, 96 opaque-font tiles.
- **Updates follow the sweep**: a track's new data applies when `scope_step() == tr_brg[i]`, which
  also pushes a trail dot (incremental: add the newest, drop the oldest). One poll per rotation.
- **Late-frame guard**: if the handler starts outside V-Blank (`GET_VDP_STATUS(VDP_VBLANK_FLAG)`),
  it only advances the sweep and returns. Otherwise active-display DMA (~18 B/line) keeps it late forever.
- Ground traffic is untagged in ALL mode (select to see). Tags keep a 1-cell gap (`TAG_SPAN`) on their right,
  and `grid_free` also refuses a spot with a label cell directly to its left (a compass `S` next to `017` read as `5017`).
- **Plane A blanks are weather:** every clear of a tag, leader, label or box cell writes the cell's weather word
  (`row_clear`, `wx_word`), and a tag's spaces show it too (`tag_row`). `grid`'s low nibble counts drawn cells; a
  tag's gap column counts `GAP` (0x10), so it blocks placement but isn't text ([weather.md](weather.md)).
- Ring labels + compass: `label_bits` (1 bit per cell, `label_at`/`label_set`) is the only record of which plane-A cells hold label text.
  `clear_labels()` erases those cells and their `grid` counts, and must run **before** anything zeroes
  `label_bits` or `grid`. Rescale order: clear tags, `scope_clear_dots`, `clear_labels`, zero `grid`,
  weather rows dirty and the map raster restarted (`radar_rescale` in main already wiped both: they were for the old
  range), `draw_labels`. Zeroing first left a wider old label's digits on screen (5 nm read `1 20 35 40 50`).
- `radar_stop()` (setup screen) removes the callback; come back with `radar_init()` + `radar_start()`
  (the track table is cleared and refilled by the next poll).
- `radar_init()` owns the whole VDP layout it needs: plane size, window, and **all four scroll values zeroed**.
  Never assume the previous screen left them at 0: cockpit mode's `voxel_flip` leaves plane A's vertical scroll at
  128 about half the time, and the scope's text then sits 16 rows down, wrapped (N below the centre, S above it).

## STARS data blocks
Themes with `leader` (STARS, ERAM) set each tag off its blip on a 1-px leader line; Classic and the others keep tags
beside the blip (pixel-identical to before: checked against `print/shots/radar.png`).
```
      GJ54505          NE / SE / NW / SW: diagonal tiles corner to corner; the tag meets the line's end corner
      246 418          E / W: a dash on the callsign row; N / S: a bar, tag above / below starting at its column
             \         lengths 1-2 cells; tried NE SE NW SW E W N S at 1, then at 2; then the 4 old spots, no line
              ◆
```
- **Tiles:** 4 (`/` `\` `-` `|`, colour 15 on transparent) after the border tiles (`t_lead = t_frame + 9`), drawn in
  the tag's palette, so the line has the tag's altitude colour.
- **State:** `track.lead` = `0x80` drawn | length << 3 | direction. Erasing keeps direction + length, and the next
  draw tries them first (`draw_lead`): a block doesn't hop from side to side each sweep while its spot stays free.
- **Cells:** leader cells are marked in `grid` like tag cells, and `clear_tag` (erase, rescale with `ungrid` off)
  rebuilds them from the tag's cell, direction and length. The notice box evicts a tag if its leader crosses the box
  (`lead_in`).
- **West side** (dx < 0): the text shifts right over the trailing spaces both lines share (`put_tag(..., right)`),
  so it meets the line; the footprint stays the tag's 8 cells.
- **Crowding:** a leader can end beside another aircraft's block in a dense spot (tags may sit over other blips,
  as before), which reads as the wrong owner at a glance. Accepted: rare outside the centre at 25 nm and wider.
- **Cost** (MAME, `RADAR_DEBUG`, STARS 25 nm, KBNA ~29 aircraft): tag stage 43 → 44 lines avg, total 105 → 107,
  no late frames.

## Measured cost (MAME, KBNA ~30 aircraft; avg scanlines of 262 per frame)
| stage | lines | how it got there |
|---|---|---|
| tile upload | ~24 | CPU writes to the data port (per-tile DMA setup + Z80 bus handshake cost 2x) |
| sweep advance | ~45 | per-tile even/odd offset segments + inline-asm `or.b`/`and.b` loops |
| crossings | ~19 | compact `tr_state`/`tr_brg` scan |
| tag redraws | ~39 | text built in main at ingest; direct VDP port row writes |
| sprites | ~31 | own sprite table, words precomputed at apply, level table |
| **total** | **~175 (67%)** | 0 late frames in steady state; the main loop gets the rest |

## Lessons (68000 @ 7.67 MHz = 488 cycles/line)
- Estimate ~2x what feels right. The first version took 261 lines/frame and starved main.
- In the V-Int: no `sprintf`, no SGDK text/sprite helpers (`VDP_drawTextEx` builds 128-entry
  buffers), no 32-bit `* / %` (`__mulsi3`, `__modsi3`: hundreds of cycles). Check the disassembly
  for `jsr __` before profiling anything else.
- Profile with V-counter deltas per stage (`RADAR_DEBUG`: panel rows 15-18 + `L<late> A<avg>`).
  `getSubTick()` and wrap-around maxima mislead when the handler spans a frame.

```c
static void radar_vint(void)    /* SYS_setVIntCallback; main never touches the VDP */
{
    if (!GET_VDP_STATUS(VDP_VBLANK_FLAG)) { scope_advance(); return; }   /* late: sweep only */
    scope_upload();                                     /* dirty tiles + glow palette */
    DMA_doDma(DMA_VRAM, spr, VDP_SPRITE_TABLE, spr_n * 4, 2);
    /* ... then CPU work: sweep step, crossings, trails, <= 2 tag redraws, sprites, panel rows */
}
```

Related: [sweep.md](sweep.md), [screen-layout.md](screen-layout.md), [config-screen.md](config-screen.md).
