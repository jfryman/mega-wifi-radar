#ifndef VOXEL_H
#define VOXEL_H

/* Cockpit mode's terrain renderer (lode/ui/cockpit.md): Voxel Space, drawn by the 68000 straight into VRAM
 * (vox.s), double-buffered with plane A's vertical scroll. Main loop only: no V-Int may touch the VDP while
 * it runs (the radar's must be stopped), or it corrupts the address between a span's command and its data. */
#include <genesis.h>
#include "arframe.h"
#include "overlay.h"

#define VOX_COLS    80          /* logical columns, 4 px each */
#define VOX_ROWS    112         /* screen rows 16..127 */
#define VOX_HUD_ROW 16          /* window plane (HUD) from this tile row down */
#define VOX_HORIZON 44          /* level flight */
#define VOX_LEVELS  32          /* row table entries: height / 2 */
#define VOX_NZ_MAX  64
#define VOX_STRIP   8           /* cells per streamed strip (a strip is VOX_STRIP x AR_TN) */

struct vox_col { s16 top; s16 roll; u32 cmd; };     /* layout shared with vox.s */

/* The patch and render state, in the mode overlay. h and c are loaded from /v1/terrain, then
 * voxel_patch() converts them in place for vox.s. They wrap: grid cell (x, y) lives at (y & 127) * 128 +
 * (x & 127), so strips of new cells (/v1/strip) overwrite the ones furthest behind. */
struct vox_ram {
    u8 h[AR_TN * AR_TN];        /* height level, then (level / 2) * 2: byte offset into the row table */
    u8 c[AR_TN * AR_TN];        /* palette index, then the byte written to VRAM (both nibbles) */
    struct vox_col cols[2][VOX_COLS];   /* per VRAM copy */
    s16 prev_top[2][VOX_COLS];  /* per copy: its terrain top last time, for the sky delta */
    s16 ytab[VOX_LEVELS];
    u16 zt[VOX_NZ_MAX];         /* depth steps, cells 8.8 */
    u8 strip[2 * VOX_STRIP * AR_TN];    /* one /v1/strip body, before it's spread into h and c */
};
#define VOX ((struct vox_ram *)(overlay_ram + OV_COCKPIT))

struct vox_cam {
    s32 x, y;                   /* cells east / north of the patch's south-west corner, 16.16 */
    s16 h;                      /* height levels above the patch's level 0 */
    u16 heading;                /* 65536 per turn */
    s16 bank;                   /* degrees, right wing down positive */
    s16 horizon;                /* screen row of the horizon (VOX_HORIZON level; lower when climbing) */
};

/* VDP set-up for the view: planes, window HUD from VOX_HUD_ROW, both VRAM copies cleared to sky. The caller
 * sets the palettes (PAL0 = the patch's). */
void voxel_init(void);
/* After loading VOX->h / VOX->c and the header: convert the maps in place and take the patch's scale. */
void voxel_patch(const struct ar_terrain *t);
/* Draw the view into the back copy. */
void voxel_render(const struct vox_cam *cam);
/* Wait for the next vblank and show the back copy. */
void voxel_flip(void);
/* Depth steps walked by the last render (for the HUD). */
u16 voxel_steps(void);

#endif
