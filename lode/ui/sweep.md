# Thin-line sweep with palette afterglow (design, agreed 2026-10-28)

A real rotating line drawn into plane B's pixels, with a phosphor tail that fades **by palette**.
Each pixel is written once when the sweep reaches it and once when the tail moves past it, and never in between.

## Geometry and budget (computed for a radius-104 disc)
| Item | Value |
|---|---|
| Angle resolution | **256 steps** per revolution (1.41°), one step per frame |
| Rotation | 256 frames = **4.27 s** NTSC (5.12 s PAL); an ASR-9 turns in 4.8 s |
| Disc tiles | **568** of the 26×26 block → RAM copy **18,176 B** |
| Disc pixels | 33,992 → per-step pixel lists **~68 KB ROM** |
| Pixels per step | 98–172 |
| Tiles touched per step | ≤ 20 → ≤ 40 dirty tiles/frame (draw + retire) → **≤ 1.3 KB DMA/frame** |

## Mechanism
```mermaid
flowchart LR
  S[step s] --> D[draw: pixels of list s<br/>nibble = glow slot of s]
  S --> R[retire: pixels of list s−TAIL<br/>nibble = 0 background]
  D --> T[mark tiles dirty]
  R --> T
  T --> Q[DMA dirty tiles in VBlank]
  P[every q frames: rotate glow palette] --> V[newest slot brightest,<br/>older slots dimmer]
```
- **Glow slots:** PAL0 indices 2..11 (G = 10). A pixel drawn at step `s` gets slot `2 + (s / q) mod G`.
  Every `q` frames the glow palette rotates by one, so the slot just written is brightest and older
  slots step down to dark. Tail = `G·q` steps (q = 3 → 30 steps ≈ 42°). Each pixel is **retired at
  age TAIL**, exactly before its slot comes round again, so slots never alias.
- **Rings and ticks are excluded from the pixel lists** at generation time. The sweep never overwrites
  them, so they stay visible under the line and need no restore.
- **The newest step is the "line":** a 1.41° wedge, about 2.5 px wide at the rim, in the brightest colour.
  The fading tail behind it gives the afterglow.

## Data (generated offline, committed)
`tools/scopegen` (Go, like the relay) writes the following into `res/`:
- `scope_tiles`: the 568 base tiles (background + rings/ticks in index 1)
- `scope_map`: the 26×26 tilemap (disc tiles → 568 unique indices; outside the disc → blank tile)
- `sweep_idx[257]` (u16 offsets) + `sweep_px[]` (u16 = `tile << 6 | pixel`), the per-step pixel lists, excluding ring pixels
The ROM copies `scope_tiles` to RAM once and DMAs it to VRAM. After that it only changes nibbles in RAM and DMAs dirty tiles.

## Invariants
- Frame cost is bounded by the largest step (172 px drawn + 172 retired, ≤ 40 tiles), never by aircraft count.
- Trails and tags are **not** in plane B (they're on plane A), so retiring a pixel to 0 never erases data.
- The STARS theme sets `sweep = false`: no drawing, and plane B shows only rings.
- RAM: 18 KB of the ~47 KB the heap has free after SGDK (measured `.data+.bss` = 5.4 KB, stack 2.5 KB).

Related: [screen-layout.md](screen-layout.md).
