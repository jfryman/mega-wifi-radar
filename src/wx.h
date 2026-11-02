#ifndef WX_H
#define WX_H

/* Weather overlay (lode/ui/weather.md): fetches the relay's weather cells when they're new, after a zoom, or
 * after the radar starts, and hands them to the renderer. Main loop only. */
#include <genesis.h>

void wx_reset(void);    /* the radar was (re)started: nothing shown */
void wx_check(void);    /* after a radar poll: fetch if needed (blocking, one small GET) */
bool wx_shown(void);    /* weather from a source is up: the panel credits it */

#endif
