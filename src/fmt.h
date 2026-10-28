#ifndef FMT_H
#define FMT_H

#include <genesis.h>

/* "36.1245" / "-86.6782" from 1e-4 degree fixed point (no floats). Returns length. */
u16 fmt_deg_e4(char *out, s32 v);
/* floor(sqrt(v)), bitwise; no division, fine for per-poll use. */
u16 isqrt32(u32 v);

#endif
