#ifndef COCKPIT_H
#define COCKPIT_H

/* Cockpit mode (lode/ui/cockpit.md): the view from one aircraft over real terrain, drawn by the cart from
 * a terrain patch and the aircraft's track (/v1/terrain, /v1/track). Takes over the VDP and the mode overlay:
 * stop the radar first, and bring it back (radar_init) after. */
#include <genesis.h>

/* Blocking: runs until B, or until the aircraft has been lost for a while. */
void cockpit_run(const u8 icao[3]);

#endif
