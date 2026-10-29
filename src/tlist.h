#ifndef TLIST_H
#define TLIST_H

/* Traffic list on the side panel (lode/ui/traffic-list.md): the tracked aircraft as
 * "CALLSIGN DIST", sorted by the current order, with the selection highlighted.
 * Main loop only: it reads the [main] side of the track table (traffic.h). */
#include <genesis.h>

#define LIST_ROWS   15

/* Hold C cycles them in this order. */
enum sort_mode { SORT_NEAREST, SORT_AZ, SORT_ZA, SORT_ALT_LOW, SORT_ALT_HIGH, SORT_MODES };

/* Re-sort from the track table (after each poll, once per sweep) and redraw.
 * The selection follows its aircraft, not its row. */
void tlist_rebuild(void);
/* Move the selection d rows (clamped); with nothing selected, selects the top row. */
void tlist_move(s16 d);
/* Next sort order (hold C); re-sorts and redraws. */
void tlist_sort_next(void);
/* Draw the header, LIST_ROWS entries and the "more" row from panel row `row`. */
void tlist_draw(u16 row);

#endif
