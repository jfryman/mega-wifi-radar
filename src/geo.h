#ifndef GEO_H
#define GEO_H

/* Fixed-point scope geometry. Positions are frame units (1/32 nm), x east,
 * y north; angles are sweep steps (256 per turn, 0 = north, clockwise). */
#include <genesis.h>

/* Bearing of (x, y) from home as a sweep step. */
u8 geo_angle(s16 x, s16 y);
/* Scale for a range: px = (pos * k) >> 15 maps range_nm to the disc radius. */
u16 geo_scale(u16 range_nm);
static inline s16 geo_px(s16 pos, u16 k) { return (s16)(((s32)pos * (s16)k) >> 15); }

#endif
