# Radar renderer (src/radar.c, scope.c, traffic.c)

After `radar_start()` the renderer **owns the VDP and runs entirely in the V-Int**
(`SYS_setVIntCallback`). The main loop never touches the VDP; it only polls the feed
(blocking HTTP is fine, the sweep keeps turning), ingests frames, and handles the pad.

```mermaid
flowchart LR
  subgraph main[main loop]
    P[poll on each north pass] --> I[traffic_ingest: next, tag text, bearing]
    J[pad: zoom/select/pan/START] --> R[radar_* requests]
  end
  subgraph vint[V-Int, every frame]
    U[V-Blank: CPU-copy dirty tiles + sprite DMA + glow palette] --> A[scope_advance]
    A --> X[apply tracks whose bearing == sweep step]
    X --> D[<= 2 tag redraws] --> S[build sprite table] --> T[<= 3 panel rows]
  end
  I -. tr_state / tr_brg / track.next .-> X
```

## Contracts
- Shared state: main writes `track.next*`, `tr_state`, `tr_brg` under `SYS_disableInts()`. The
  V-Int owns `cur`, trails, tags and sprites. `radar_panel()` copies text under ints-off.
- **Updates follow the sweep**: a track's new data applies when `scope_step() == tr_brg[i]`, which
  also pushes a trail dot (incremental: add the newest, drop the oldest). One poll per rotation.
- **Late-frame guard**: if the handler starts outside V-Blank (`GET_VDP_STATUS(VDP_VBLANK_FLAG)`),
  it only advances the sweep and returns. Otherwise active-display DMA (~18 B/line) keeps it late forever.
- Ground traffic is untagged in ALL mode (select to see). Tags keep a 1-cell gap (`TAG_SPAN`) on their right,
  and `grid_free` also refuses a spot with a label cell directly to its left (a compass `S` next to `017` read as `5017`).
- Ring labels + compass: `label_cells` is the only record of which plane-A cells hold label text.
  `clear_labels()` erases those cells and their `grid` counts, and must run **before** anything zeroes
  `label_cells` or `grid`. Rescale order: clear tags, `scope_clear_dots`, `clear_labels`, zero `grid`,
  `draw_labels`. Zeroing first left a wider old label's digits on screen (5 nm read `1 20 35 40 50`).
- `radar_stop()` (setup screen) removes the callback; come back with `radar_init()` + `radar_start()`
  (the track table is cleared and refilled by the next poll).

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

Related: [sweep.md](sweep.md), [screen-layout.md](screen-layout.md), [config-screen.md](config-screen.md).
