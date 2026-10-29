# Detail pop-up (`src/popup.c`, renderer side in `src/radar.c`)

Tap C on the radar: a full-height panel over the scope with everything known about the selected aircraft.
The radar keeps running underneath, the side panel stays visible, ▲/▼ step the selection, and C or B closes it.

```
row 1  SWA692            AC379A     callsign (PAL2, red if emergency), ICAO address
row 3  N8868L   B38M  BUILT 2023    detail: reg, type, year
row 4  BOEING 737 MAX 8             detail: desc
row 5  SOUTHWEST AIRLINES CO        detail: owner (PAL1)
rows 7-13   ALTITUDE / VERT SPEED (frame) · GPS ALT / SEL ALT / SEL HDG / ALTIMETER (inHg) / QNH (hPa) (detail)
rows 15-18  SPEED / TRACK / SQUAWK + meaning / CATEGORY + name (frame)
rows 20-23  DISTANCE / BEARING FROM <icao> / STATUS / LAST SEEN (detail)
row 25 UP/DOWN NEXT   C/B CLOSE
```
Labels ≤ 10 chars (`LABEL_W` 11 with the pad), values from col 12 to col 26. `--` = not reported.
Row 3 shows `LOADING DETAILS...` until the fetch returns, or `NO DETAILS FROM THE RELAY`.

## Data
- **Live** values come from `tracks[sel].next` (the newest frame); redrawn after every poll.
- **Detail** comes from `feed_detail()` → `GET /v1/detail?icao=&v=` (struct `ar_detail`,
  [../architecture/contract.md](../architecture/contract.md)): fetched on open, after every poll while open, and
  when the selection has been still for `SETTLE_FRAMES` (15) after a ▲/▼ (no fetch per auto-repeat step).
  A failed refresh keeps the last good record for the same aircraft.
- The emulator build answers from `res/fake/kbna_detail.bin` (`relay -pack-detail`, `make fake-feed`).
- The pop-up closes itself if the aircraft leaves the feed, and before setup opens.

## Rendering (radar.c)
- Drawn on the **window plane**, cols 0..27, all 28 rows: `VDP_setWindowVPos(FALSE, PANEL_ROWS)` makes the window
  cover the scope; the side panel (window cols 28+) is unaffected. Closing sets VPos back to 0: instant.
- **Opaque font:** `opaque_font()` reads SGDK's font back from VRAM at `radar_init` and writes a copy with index 0
  → `C_OPAQUE` (14, black in every palette of both themes) at `t_ofont`, after the blip/box tiles. Window pixels
  of index 0 are transparent and would show plane B (the scope) through.
- Rows go out like the panel's: `popup_dirty` bits, `POPUP_PER_FRAME` (3) 28-cell rows per V-Int. The window
  switches on only once every row is out (~10 frames). **Opening marks all 28 rows dirty**: blank rows that
  match the buffer would otherwise never be written and stay transparent (seen as scope fragments).
- Sprites are high priority and would draw over the window, so `sprites()` emits none while it's up.
- Plane A (tags, trails) keeps updating under the window.

```mermaid
sequenceDiagram
  participant M as main loop (popup.c)
  participant R as radar.c V-Int
  participant F as relay
  M->>R: radar_popup_row x28, radar_popup(TRUE) (all rows dirty)
  R->>R: write 3 rows/frame; when done: window VPos = 28, hide sprites
  M->>F: feed_detail (blocking; V-Int keeps sweeping)
  F-->>M: ar_detail
  M->>R: radar_popup_row (changed rows only)
  M->>R: radar_popup(FALSE) on C / B: window VPos = 0
```

## Input (main.c `handle_input`)
C acts on **release** before `HOLD_FRAMES` (40): tap toggles the pop-up; reaching 40 frames held cycles the sort
instead. While open, B closes and does **not** zoom (A/B zoom is off while it's up).

Related: [traffic-list.md](traffic-list.md), [radar-renderer.md](radar-renderer.md), [../net/feed.md](../net/feed.md),
[../relay/relay.md](../relay/relay.md).
