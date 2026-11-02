#ifndef TLIST_H
#define TLIST_H

/* Traffic list on the side panel (lode/ui/traffic-list.md): tabs, switched with A / B. The list tabs show the tracked
 * aircraft as "CALLSIGN DIST" in their order, with the selection highlighted; the LOGBOOK tab hands the rows to
 * logbook.c. Main loop only: it reads the [main] side of the track table (traffic.h). */
#include <genesis.h>

#define LIST_ROWS   15

/* settings.sort: the tab. Values are saved, so they never change: SORT_ZA (0.4.x) is no tab any more and loads as
 * A-Z. The tabs run NEAREST, LOWEST, HIGHEST, A-Z, LOGBOOK. */
enum sort_mode { SORT_NEAREST, SORT_AZ, SORT_ZA, SORT_ALT_LOW, SORT_ALT_HIGH, SORT_LOGBOOK, SORT_MODES };

/* Re-sort from the track table (after each poll, once per sweep) and redraw.
 * The selection follows its aircraft, not its row. */
void tlist_rebuild(void);
/* Move the selection d rows (clamped); with nothing selected, selects the top row. */
void tlist_move(s16 d);
/* Next (d = 1) or previous (-1) tab (A / B); re-sorts and redraws. */
void tlist_tab(s16 d);
/* Draw the header, LIST_ROWS entries and the "more" row from panel row `row`. */
void tlist_draw(u16 row);

#endif
