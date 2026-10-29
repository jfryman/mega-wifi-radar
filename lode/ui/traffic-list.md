# Traffic list (`src/tlist.c`)

The side panel's main block: every tracked aircraft as `CALLSIGN DIST`, in a chosen order, with the selection
highlighted. ▲/▼ move the selection; hold C cycles the order. It replaced the on-screen control help and the
selected-aircraft lines (those details live in the [detail pop-up](detail-popup.md)).

```
row  6  - NEAREST -     header: sort order (sort_names[], each <= 11 chars)
row  7  UAL2414 0.5     LIST_ROWS (15) entries: cs cut to 7, a space, distance (3 chars)
 ...    SWA692  5.6     selected: PAL2 (cyan); emergency: PAL_TEXT_EMERG (red); others PAL0
row 21  N8065L   14
row 22  ^2 v11 MORE     hidden above / below; blank when everything fits
```

## Behaviour
- **Orders** (`enum sort_mode`, RAM only, every boot starts at NEAREST): NEAREST, A-Z, Z-A, LOW ALT, HIGH ALT.
  Emergencies (`AR_F_EMERG`) come first in every order. Ties keep slot order, so the list is stable.
- **Altitude key:** ground lowest, unknown altitude after every known one in both altitude orders.
- **Data:** the `[main]` side of the track table (`tracks[i].next`, used && !drop), so it never races the V-Int.
  Distance = `isqrt32(x² + y²)` in 1/32 nm, computed once per rebuild.
- **Rebuild** (`tlist_rebuild`): after every poll (once per sweep, as it passes north), on a sort change, and when
  `radar_up()` empties the table. Not continuous, so rows don't jump under the cursor.
- **Selection** is a track slot (`radar_select`), so it follows the aircraft, not the row. A dropped aircraft
  clears it (the V-Int sets `selected = -1`; rebuild also checks). `scroll_to` keeps it on screen.
- **Moving** (`tlist_move(d)`): clamps at the ends (no wrap); with nothing selected, selects the top row.
  `tlist_move(0)` is "select the top if none" (the pop-up uses it). TAGS SEL mode retags on every move.

```mermaid
flowchart LR
  P[poll: traffic_ingest] --> R[tlist_rebuild: insertion sort<br/>emergencies first, then sort_mode]
  H[hold C] --> S[tlist_sort_next] --> R
  U[▲ ▼ with auto-repeat] --> M[tlist_move] --> D
  R --> D[tlist_draw: radar_panel_pal per row]
  D --> W[V-Int panel(): 3 rows/frame, skips unchanged rows]
```

## Contracts
- Main loop only (it calls `radar_panel_pal`, which is safe there). `tlist_draw(row)` must run once before
  `tlist_rebuild` redraws (it records the first row; `panel_static` does both).
- `radar_panel_pal` is a no-op when the row's text and palette are unchanged, so redrawing all 17 rows each
  sweep costs nothing on the V-Int when little moved.
- Insertion sort on ≤ 96 entries once per sweep: fine in the main loop, never in the V-Int.

```c
/* main.c panel rows */
ROW_LIST,                               /* sort header, LIST_ROWS entries, "more" (tlist.c) */
ROW_SEP = ROW_LIST + LIST_ROWS + 2, ROW_MODES, ROW_DATA1, ROW_DATA2,
_Static_assert(ROW_DATA2 == 26, "the data credit must sit on the last safe row");
```

Related: [screen-layout.md](screen-layout.md), [detail-popup.md](detail-popup.md), [radar-renderer.md](radar-renderer.md).
