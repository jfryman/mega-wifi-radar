/* Cockpit mode on the 32X: shared between the slave's control (sh/cockpit32.c) and the master's renderer
 * (sh/rvox.c). lode/mars/renderer.md. */
#ifndef RVOX_H
#define RVOX_H
#include "genesis.h"
#include "arframe.h"

#define VOX_W       160         /* columns, 2 px each */
#define VOX_H       160         /* view rows; the HUD is below */
#define VOX_HORIZON 64          /* level flight */

enum { CK_LIVE, CK_STALE, CK_LOADING, CK_LOST };

/* /v1/terrain's body, as received: raw levels (0..63) and palette indices (4..15). The maps wrap: grid cell
 * (x, y) is at (y & 127) * 128 + (x & 127). Written by the slave (in its cache-write-through SDRAM), read by the
 * master after a purge (sh.ck.terrain_gen). */
struct ck_terrain {
    struct ar_terrain hdr;
    u8 h[AR_TN * AR_TN];
    u8 c[AR_TN * AR_TN];
};
extern struct ck_terrain ck_terrain;

#endif
