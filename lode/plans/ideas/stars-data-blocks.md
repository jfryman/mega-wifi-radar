# Goal: STARS data blocks with leader lines (idea, 2026-11-01)

**Outcome:** in the STARS and ERAM themes, tags look like real STARS full data blocks: offset from the target by a
short **leader line**, in one of 8 directions, with a time-shared second line. Classic keeps its tags beside the
blip.

```
 now (all themes)         goal (STARS / ERAM)
   ◆ SWA271                 SWA271          line 1: callsign
     016 174               /016 17          line 2: altitude (hundreds ft) + ground speed (tens kt)
                          /                 alternates every ~2 s with: type  e.g. B38M  (needs the type, below)
                         ◆                  leader line: 1-2 cells, 8 directions
```

## Design
```mermaid
flowchart LR
  T[tag placement: 4 candidates today] --> T8[8 directions x 1-2 cells,<br/>first free in grid]
  T8 --> L[leader tiles on plane A:<br/>/ \ - | per direction]
  L --> D[tag text after the line]
  S[every ~2 s] --> X[swap line 2: alt+gs <-> type]
```
- **Leader line tiles:** a handful of plane-A tiles (diagonal `/` `\`, horizontal, vertical, each drawn from a
  cell corner) in the tag's palette. The blip is a sprite at pixel resolution and tags are cell-aligned, so the
  line starts at the cell next to the blip; close enough at 8 px.
- **Placement:** today `grid_free` tries 4 spots around the blip ([../../ui/radar-renderer.md](../../ui/radar-renderer.md)).
  STARS mode tries 8 directions at distance 1 then 2 cells; the line's cells are reserved in `grid` like tag cells.
- **Theme flag:** `bool leader` in `struct theme` (STARS, ERAM on). No settings change.
- **Time-shared line 2** (optional, phase 2): STARS alternates altitude/speed with type/destination. The record has
  no type (`ar_detail` does, 96 B per request: too heavy per aircraft). Either add the type to `ar_rec` (record
  grows 24 → 28: ~1.2 KB more RAM in tracks + frame buffer; see [../watch-arrivals.md](../watch-arrivals.md) for the same
  trade-off) or skip it. Phase 1 needs no contract change.
- **Cost:** tag redraws are capped at 2 per frame (~39 lines); a leader adds 1-2 words per tag. Main builds the tag
  text and line tiles at ingest, as now.

## Risks / questions
- Crowding: longer tags need more free space; at 25 nm around KORD, more tags will fail to place. Fall back to the
  4 adjacent spots without a line.
- Leader direction stability: a tag shouldn't hop direction every sweep; prefer the previous direction when free.
- STARS speed is in tens of knots (2 digits) and altitude 3 digits: confirm the owner wants that over today's format.

## Done when
- [ ] Theme flag + 8-direction placement + leader tiles; Classic unchanged (pixel-identical `make sim` shot).
- [ ] Tags keep their direction between sweeps when the spot stays free.
- [ ] V-Int stays within budget (`RADAR_DEBUG`, KORD-sized traffic in `make sim`).
- [ ] Manual Themes page STARS shot updated; on the cart at 10 and 25 nm.
- [ ] (Phase 2, optional) type in the record, alternating line 2; contract bump.

Related: [summary.md](summary.md), [../../ui/screen-layout.md](../../ui/screen-layout.md).
