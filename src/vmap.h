#ifndef VMAP_H
#define VMAP_H

/* Video maps (lode/ui/video-maps.md): fetches the relay's runways + centrelines for the home point at the current
 * range when the radar starts and after a zoom, and hands them to the renderer. Main loop only. */
#include <genesis.h>

void vmap_reset(void);  /* the radar was (re)started: nothing drawn */
void vmap_check(void);  /* after a radar poll: fetch if needed (blocking, one small GET) */

#endif
