#ifndef SCOPE_H
#define SCOPE_H

/* Plane-B radar disc: rings, thin-line sweep with palette afterglow, trail
 * dots. Design: lode/ui/sweep.md. Everything except scope_init() runs in the
 * VInt (it owns the VDP once the radar starts). */
#include <genesis.h>
#include "theme.h"

#define SCOPE_X     8       /* disc block top-left on screen (px) */
#define SCOPE_Y     8
#define SCOPE_CX    104     /* disc centre inside the block */
#define SCOPE_CY    104
#define SCOPE_R     104
#define SCOPE_STEPS 256

/* Loads tiles/map/palettes; call before the radar's VInt starts. */
void scope_init(const struct theme *t, u16 vram_base);
/* Retheme without reloading tiles (palettes + sweep on/off). VInt context. */
void scope_set_theme(const struct theme *t);
/* Start of V-Blank: DMA the tiles dirtied last frame + glow palette. */
void scope_upload(void);
/* After the upload: advance the sweep one step (RAM only). */
void scope_advance(void);
/* Current sweep step, 0 = north, clockwise. */
u8 scope_step(void);
/* 2x2 trail dot at block coords (px, py). VInt. */
void scope_dot(s16 px, s16 py, bool on);
/* Remove every trail dot (zoom/pan redraw). VInt. */
void scope_clear_dots(void);
/* First VRAM tile after the scope's tiles. */
u16 scope_vram_end(void);

#endif
