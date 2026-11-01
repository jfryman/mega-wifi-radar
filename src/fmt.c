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

void fmt_thousands(char *buf, s32 v)
{
    char tmp[12];
    u16 n = 0, len;
    bool neg = v < 0;
    u32 u = neg ? -v : v;

    do { tmp[n++] = '0' + u % 10; u /= 10; } while (u);
    len = 0;
    if (neg) buf[len++] = '-';
    while (n) {
        buf[len++] = tmp[--n];
        if (n && !(n % 3)) buf[len++] = ',';
    }
    buf[len] = 0;
}
