# Radar screen layout (agreed 2026-10-28)

320×224 (H40), 40×28 tiles. Everything that matters stays **≥ 8 px inside the frame**: the owner's
TV crops the bottom text row and the right edge (measured on the spike). Usable area: rows 1–26, cols 1–38.

```
 col 0 1                        2 2 2        3       3 3
     0 1                        6 7 8        5       8 9
row 0  ·· overscan margin ··························· ··
row 1   ·        .----N----.        │ KBNA          │
        ·     .-'  ·  25  · '-.     │ RNG  25 NM    │
        ·   .'  ·  20 ·  ·    '.    │ 17:42:05Z     │
        ·  /  · 15 ·  ·  ·     \    │ 22 AC  NET OK │
        · |  · 10 · ┌SWA271┐  · |   │───────────────│
        · | ·  5 ·  │016 174│  · |  │ SWA271        │
row 13  · W ·  ·  · +◆  ·  ·  ·  E  │ ALT 016  ↑    │
        · |  ·  ·  :·  · ┌DAL1046┐  │ GS 174 TRK 090│
        ·  \  ·  ·  ·  · │350 472│  │ SQK 4271      │
        ·   '.  ·  ·  ·  ◆·:· .'    │ 1.8NM BRG 045 │
        ·     '-.  ·  ·  .-'        │───────────────│
row 26  ·        '----S----'        │ Data: adsb.fi │
row 27  ·· overscan margin ·········│···············│
```

## Regions
- **Scope:** disc radius **104 px**, centre **(112, 112)**, in a 208×208 block at pixel (8, 8)
  (tiles 1–26). Home (`+`) at the centre. **5 range rings**, evenly spaced. The art never changes;
  zoom only relabels them (25 nm → 5/10/15/20/25). Compass N/E/S/W at the rim; ring labels along the NE diagonal.
- **Side panel:** cols 28–38 (x 224–311), rows 1–26, on the **window** plane. From the top: airport,
  range, UTC time (SNTP), aircraft count + network state (`NET OK` / `STALE 12s` / `ERR`),
  then the selected aircraft (callsign, ALT + trend, GS, TRK, SQK, distance and bearing), then
  **`Data: adsb.fi`**, which is required and always visible.

## Layers
| Layer | Contents | Notes |
|---|---|---|
| Plane B | scope disc: rings/ticks + **thin-line sweep** with afterglow | 568 unique tiles, RAM copy, see [sweep.md](sweep.md) |
| Plane A | data tags (tile-aligned text) + **trail dots** in a pool of dynamic tiles | one tile per cell: a tag hides trail dots under it |
| Window | side panel | fixed; doesn't pan |
| Sprites | one 8×8 blip per aircraft (4 brightness tiles), selection box, emergency marker | ≤ 80 total, ≤ 20 per line |

## Scaling
Records are in 1/32 nm. Screen offset `px = (pos × K) >> 16`, with `K = 104 · 65536 / (range_nm · 32)`.
That's one `MULS` (16×16 → 32) per axis, with no software maths. Ranges: **5 / 10 / 25 / 50 / 100 nm** (A/B).

## Behaviour
- **Trails:** the last **5** positions per aircraft, one sampled per sweep rotation (~21 s of history), dimmer with age.
- **Updates follow the sweep:** a new frame's position for an aircraft is applied when the sweep crosses
  its bearing, and its blip brightness tracks time since that crossing. **One poll per rotation**, fired as the
  sweep passes north. This hides network latency.
- **Tags:** 2 lines (`SWA271` / `016 174`: alt in hundreds of ft, gs kt), coloured by altitude band.
  Placed in the first free spot of 4 candidates around the blip. Holding C cycles *all / selected / off*.
- **Controls:**

  | Button | Action |
  |---|---|
  | A / B | zoom in / out |
  | C | select the next aircraft (nearest first) |
  | D-pad | pan (scroll planes A and B, offset sprites) |
  | START | settings |

## Themes (config option)
A theme is data: four palettes plus a few flags. Switching one reloads the palettes and toggles features,
with no change to the layer layout.

| | **Classic** (default, built first) | **STARS** (later) |
|---|---|---|
| Look | green phosphor on black | black background, white/cyan tags, blue rings |
| Sweep | thin line + afterglow | none (targets just update each scan) |
| Blips | phosphor fade after the sweep | constant brightness |

```c
struct theme { u16 pal[4][16]; bool sweep; bool blip_fade; };
```

Related: [sweep.md](sweep.md), [../net/feed.md](../net/feed.md), [../architecture/frame-format.md](../architecture/frame-format.md).
