# Radar screen layout

320×224 (H40), 40×28 tiles. Everything that matters stays **≥ 8 px inside the frame**: the owner's
TV crops the bottom text row and the right edge (measured on the spike). Usable area: rows 1–26, cols 1–38.

```
 col 0 1                        2 2 2        3       3 3
     0 1                        6 7 8        5       8 9
row 0  ·· overscan margin ··························· ··
row 1   ·        .----N----.        │ KBNA          │
        ·     .-'  ·  25  · '-.     │ RNG  25NM     │
        ·   .'  ·  20 ·  ·    '.    │ 17:42:05Z     │
        ·  /  · 15 ·  ·  ·     \    │ 28 AC         │
        · |  · 10 · ┌SWA271┐  · |   │ NET OK        │
row 6   · | ·  5 ·  │016 174│  · |  │ - NEAREST -   │  traffic list header
row 7   · W ·  ·  · +◆  ·  ·  ·  E  │ UAL2414 0.5   │  15 rows (lode: traffic-list.md)
        · |  ·  ·  :·  · ┌DAL1046┐  │ SWA271  1.8   │  selected: cyan
        ·  \  ·  ·  ·  · │350 472│  │ ...           │
row 22  ·   '.  ·  ·  ·  ◆·:· .'    │ v13 MORE      │
row 23  ·     '-.  ·  ·  .-'        │ -----------   │
row 24  ·        '----S----'        │ CLASSIC ALL   │  theme + tag mode
row 25                              │ Data:         │
row 26                              │ adsb.fi       │  required, always visible
row 27  ·· overscan margin ·········│···············│
```

## Regions
- **Scope:** disc radius **104 px**, centre **(112, 112)**, in a 208×208 block at pixel (8, 8)
  (tiles 1–26). Home (`+`) at the centre. **5 range rings**, evenly spaced. The art never changes;
  zoom only relabels them (25 nm → 5/10/15/20/25). Compass N/E/S/W at the rim; ring labels along the NE diagonal.
- **Side panel:** cols 28–38 (x 224–311), rows 1–26, on the **window** plane. From the top: airport,
  range, UTC time (SNTP), aircraft count, network state (`NET OK` / `STALE 12s` / `ERR HTTP` / `RELAY VER?`),
  then the [traffic list](traffic-list.md) (header, 15 rows, "more" row), a rule, theme + tag mode, and
  **`Data: adsb.fi`**, which is required and always visible (`adsb.fi IEM` while weather is shown). Each row has its own text palette.
- **Detail pop-up:** window cols 0–27 over the scope while it's open ([detail-popup.md](detail-popup.md)).
- **Flyover notice box:** plane A, rows 11–17 × cols 1–26 over the scope's centre, grows open for 15 s when a notice arrives; tags move around it ([notices.md](notices.md)).

## Layers
| Layer | Contents | Notes |
|---|---|---|
| Plane A (high) | data tags (tile-aligned text), ring labels, compass; the flyover notice box | trail dots are on plane B |
| Plane B (high) | scope disc: rings/ticks + **thin-line sweep** with afterglow | 568 unique tiles, RAM copy, see [sweep.md](sweep.md) |
| Plane A (low) | weather stipple in the cells without text ([weather.md](weather.md)) | under the scope's lines, over its background |
| Window | side panel; the detail pop-up over cols 0–27 while open | fixed: the scope never scrolls |
| Sprites | one 8×8 blip per aircraft (4 brightness tiles), selection box, emergency marker | ≤ 80 total, ≤ 20 per line |

## Scaling
Records are in 1/32 nm. Screen offset `px = (pos × K) >> 16`, with `K = 104 · 65536 / (range_nm · 32)`.
That's one `MULS` (16×16 → 32) per axis, with no software maths. Ranges: **5 / 10 / 25 / 50 / 100 nm** (A/B).

## Behaviour
- **Trails:** the last **5** positions per aircraft, one sampled per sweep rotation (~21 s of history), dimmer with age.
- **Updates follow the sweep:** a new frame's position for an aircraft is applied when the sweep crosses
  its bearing, and its blip brightness tracks time since that crossing. **One poll per rotation**, fired as the
  sweep passes north. This hides network latency.
- **Tags:** 2 lines (`SWA271` / `016 174`: alt in hundreds of ft, gs kt), coloured by altitude band. Themes with
  `leader` (STARS, ERAM) draw them as data blocks on a leader line ([radar-renderer.md](radar-renderer.md#stars-data-blocks)).
  Placed in the first free spot of 4 candidates around the blip. ◀/▶ cycle *all / selected / off*.
- **Controls:**

  | Button | Action |
  |---|---|
  | A / B | zoom in / out (off while the pop-up is open; B closes it, or a flyover notice) |
  | ▲ / ▼ | previous / next aircraft in the traffic list (auto-repeat: 16 frames, then every 5) |
  | ◀ / ▶ | tags: all / selected / off |
  | C tap | detail pop-up open / close (acts on release); with a flyover notice up, for its aircraft |
  | A (pop-up open) | cockpit mode for that aircraft ([cockpit.md](cockpit.md)); B there goes back |
  | C hold (40 frames) | next sort order: NEAREST, A-Z, Z-A, LOW ALT, HIGH ALT |
  | X (6-button) | next theme; a shortcut only |
  | Y (6-button) | cockpit mode for the selected aircraft (the first in the list if none); a shortcut only |
  | Z (6-button) | weather on / off (setup's WEATHER row); a shortcut only |
  | START | setup |

  A 3-button pad must reach everything: 6-button buttons only duplicate setup options or pop-up actions. There is no pan:
  the scope is fixed on the home airport (it scrolled the planes but helped nobody, so 0.2.0 removed it).

```mermaid
flowchart TB
  SP[Sprites: blips, selection box, emergency marker] --> A
  A[Plane A high: data tags + ring/compass labels] --> B
  B[Plane B high: scope disc, sweep, trail dots] --> WX
  WX[Plane A low: weather cells]
  W[Window: side panel + detail pop-up, fixed]
```

## Themes (config option)
A theme is data: four palettes plus a few flags. Switching one reloads the palettes and toggles features,
with no change to the layer layout.

Nine themes, cycled in setup's THEME row or by X; `settings.theme` is their index (an index past
`THEME_COUNT` invalidates the saved record, so only ever append to the enum).

| Theme | Look | Sweep + blip fade |
|---|---|---|
| **Classic** (default) | green phosphor on black | yes |
| **STARS** | black, white/cyan data blocks on leader lines, blue rings | no: targets just update each scan |
| AMBER | amber phosphor, orange sweep, red emergencies | yes |
| NIGHT | dim reds; tags red → orange with altitude, emergencies pale pink | yes |
| MARINE | yellow-orange on navy (`0x200`), steel-blue rings | yes |
| ICE | blue-white phosphor | yes |
| POCKET | dark olive on pea green (`0x0A8`), brown emergencies | no |
| SUNSET | cyan sweep over purple (`0x402`), magenta rings, pink trails | yes |
| ERAM | grey-green maps, green targets, pale data blocks on leader lines | no |

```c
struct theme { const char *name; u16 pal[4][16]; u16 glow[GLOW_SLOTS]; bool sweep; bool blip_fade; bool leader; };
```

Palette contract (`theme.h`), the same for every theme:
- PAL0: 0 background (also the backdrop), 1 rings, 2-11 glow slots (written at runtime from `glow`, newest first;
  the last step should approach the background), 12 trails, 13 weather (`C_WX`, dim), 15 white-ish text (high band + the panel).
- PAL1: 1-4 blip brightness levels (all equal without `blip_fade`), 5 selection box, 6 stale; 15 low-band text.
- PAL2: 15 mid-band text (and the selected list row). PAL3: 1-4 emergency blips, 15 emergency text.
- **Index 14 of every palette = the scope's background**: the opaque font and box borders fill with it
  (`C_OPAQUE`), so pop-ups and the notice box match a non-black background (MARINE, POCKET, SUNSET).
- Without a sweep the glow slots take the background colour (`set_glow_palette`), not black.
- The four text bands must stay distinct, and emergencies must stand out from all three.
- Names ≤ 7 characters (panel row `%-7s ALL`).

Related: [sweep.md](sweep.md), [../net/feed.md](../net/feed.md), [../architecture/frame-format.md](../architecture/frame-format.md).
