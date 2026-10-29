#ifndef POPUP_H
#define POPUP_H

/* Detail pop-up for the selected aircraft (lode/ui/detail-popup.md). Main loop only: it composes
 * the rows (radar_popup_row) from the live track and the relay's detail record (feed_detail). */
#include <genesis.h>

/* Open for the selection (selects the top of the list if nothing is). Fetches the detail. */
void popup_open(void);
void popup_close(void);
bool popup_is_open(void);
/* The selection moved: redraw now, fetch its detail once it settles (see popup_tick). */
void popup_selection_changed(void);
/* After each frame poll: redraw the live rows and refresh the detail. Closes if the aircraft left. */
void popup_after_poll(void);
/* Every frame: fetch a pending detail once the selection has been still for a moment. */
void popup_tick(void);

#endif
