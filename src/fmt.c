#include "fmt.h"

u16 fmt_deg_e4(char *out, s32 v)
{
    u32 a = v < 0 ? -v : v;
    return sprintf(out, "%s%lu.%04lu", v < 0 ? "-" : "", a / 10000, a % 10000);
}

u16 isqrt32(u32 v)
{
    u32 res = 0, bit = 1UL << 30;

    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}
