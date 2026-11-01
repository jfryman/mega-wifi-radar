#ifndef OVERLAY_H
#define OVERLAY_H

/* RAM that one screen mode owns at a time (lode/build/ram-budget.md): the radar's scope tiles, trail
 * bitmap and track table, or cockpit mode's terrain patch and render state (lode/ui/cockpit.md). Entering
 * a mode initialises its part (radar_init, cockpit_run); nothing in it survives a switch. Each user reaches
 * its arrays through a constant address (overlay_ram + offset), so code is the same as for a static array. */
#include <genesis.h>

#define SCOPE_NTILES    568                             /* must match tools/scopegen output */
#define OV_SCOPE_TILES  0                               /* u32[SCOPE_NTILES * 8]: scope.c */
#define OV_TRAIL_BITS   (OV_SCOPE_TILES + SCOPE_NTILES * 32)    /* u8[SCOPE_NTILES * 8]: scope.c */
#define OV_TRACKS       (OV_TRAIL_BITS + SCOPE_NTILES * 8)      /* struct track[TR_MAX]: traffic.h */
#define OV_COCKPIT      0                               /* struct vox_ram: voxel.h */

extern u8 overlay_ram[];

#endif
