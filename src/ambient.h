#ifndef AMBIENT_H
#define AMBIENT_H

/* Ambient mode (lode/ui/ambient.md): left alone for settings.ambient's minutes, the radar tours flights by
 * itself: it picks one, opens its details, rides along in cockpit mode, comes back, and does it again. Any
 * button hands control back. Main loop only. */
#include <genesis.h>

enum ambient_act { AMB_NONE, AMB_PANEL, AMB_COCKPIT };

/* Every radar frame, with the pad state: a button held or pressed counts as activity. TRUE when the press
 * ended ambient mode, so the caller swallows it (until every button is released). */
bool ambient_input(u16 joy);
/* Every radar frame, after input. AMB_PANEL: ambient mode started or stopped (redraw the panel's status);
 * AMB_COCKPIT: ride ambient_icao() for AMBIENT_RIDE_SECS, then report with ambient_back(). */
enum ambient_act ambient_tick(void);
const u8 *ambient_icao(void);
/* Back on the radar after a ride; by_user if a button ended it (that ends ambient mode too). */
void ambient_back(bool by_user);
bool ambient_on(void);

#define AMBIENT_RIDE_SECS   75

#endif
