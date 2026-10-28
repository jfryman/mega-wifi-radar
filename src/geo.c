#include "geo.h"
#include "scope.h"

/* round(atan(i/64) * 128/pi): sweep steps for i = 0..64 (0..45 deg = 0..32 steps). */
static const u8 atan_tab[65] = {
     0,  1,  1,  2,  3,  3,  4,  4,  5,  6,  6,  7,  8,  8,  9,  9,
    10, 11, 11, 12, 12, 13, 13, 14, 15, 15, 16, 16, 17, 17, 18, 18,
    19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 25, 26,
    26, 27, 27, 27, 28, 28, 29, 29, 29, 30, 30, 30, 31, 31, 31, 32,
    32,
};

u8 geo_angle(s16 x, s16 y)
{
    u16 ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    u8 a;

    if (!ax && !ay) return 0;
    /* angle from north toward east within the first quadrant */
    if (ax <= ay) a = atan_tab[((u32)ax << 6) / ay];
    else a = 64 - atan_tab[((u32)ay << 6) / ax];
    if (x >= 0) return y >= 0 ? a : 128 - a;     /* NE / SE */
    return y >= 0 ? (u8)(256 - a) : 128 + a;       /* NW / SW */
}

u16 geo_scale(u16 range_nm)
{
    /* SCOPE_R px = range_nm * 32 units  =>  k = SCOPE_R * 32768 / (range_nm * 32) */
    return (u16)(((u32)SCOPE_R << 10) / range_nm);
}
