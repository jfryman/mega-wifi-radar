#ifndef LOGBOOK_H
#define LOGBOOK_H

/* Spotter's logbook on the cart (lode/ui/logbook.md): the LOGBOOK tab's panel rows and the full page (the pop-up's
 * window rows). The relay keeps the log; this fetches it (feed_log) and writes the text at once, keeping no copy.
 * Main loop only. */
#include <genesis.h>

void logbook_panel(u16 row);    /* tlist_draw on the LOGBOOK tab: its rows from `row` (LOADING until fetched) */
void logbook_forget(void);      /* the rows were drawn over (another tab, the radar restarted) */
void logbook_fetch(void);       /* fetch now and redraw the panel rows (and the page, if open); blocking */
void logbook_poll(void);        /* after a radar poll: refresh once a minute while the tab shows */
void logbook_page(bool open);   /* the full page over the scope (C on the LOGBOOK tab) */
bool logbook_page_open(void);

#endif
