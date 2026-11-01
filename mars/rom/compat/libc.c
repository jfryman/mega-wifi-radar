/* The C library the 32X ROM's CPUs need (no libc on either): string helpers and an sprintf covering the
 * formats the shared src files use (%s %c %d %u %x %X %p, flags - 0, width or *, l). Like SGDK. */
#include "genesis.h"

void *memcpy(void *d, const void *s, size_t n)
{
    u8 *dp = d;
    const u8 *sp = s;
    if (!(((uintptr_t)dp | (uintptr_t)sp) & 3))
        for (; n >= 4; n -= 4, dp += 4, sp += 4) *(u32 *)dp = *(const u32 *)sp;
    while (n--) *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    u8 *dp = d;
    const u8 *sp = s;
    if (dp <= sp || dp >= sp + n) return memcpy(d, s, n);
    while (n--) dp[n] = sp[n];
    return d;
}

void *memset(void *d, int c, size_t n)
{
    u8 *dp = d;
    if (!((uintptr_t)dp & 3)) {
        u32 w = (u8)c * 0x01010101UL;
        for (; n >= 4; n -= 4, dp += 4) *(u32 *)dp = w;
    }
    while (n--) *dp++ = (u8)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *x = a, *y = b;
    for (; n--; x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) {}
    return r;
}

/* as SGDK's: copies at most n, NUL-terminates if room, doesn't pad */
char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n && (*d = *s)) { d++; s++; n--; }
    if (n) *d = 0;
    return r;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

static char *put_num(char *o, u32 v, u16 base, bool upper, bool neg, s16 width, bool left, bool zero)
{
    char tmp[12];
    s16 n = 0, len;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    do { tmp[n++] = dig[v % base]; v /= base; } while (v);
    len = n + neg;
    if (!left && !zero) while (width-- > len) *o++ = ' ';
    if (neg) *o++ = '-';
    if (!left && zero) while (width-- > len) *o++ = '0';
    while (n) *o++ = tmp[--n];
    if (left) while (width-- > len) *o++ = ' ';
    return o;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    char *o = buf;

    for (; *fmt; fmt++) {
        if (*fmt != '%') { *o++ = *fmt; continue; }
        bool left = FALSE, zero = FALSE;
        s16 width = 0;
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-') left = TRUE;
            else if (*fmt == '0') zero = TRUE;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'h') fmt++;
        switch (*fmt) {
        case 'd': case 'i': {
            s32 v = va_arg(ap, s32);
            o = put_num(o, v < 0 ? (u32)-v : (u32)v, 10, FALSE, v < 0, width, left, zero);
            break;
        }
        case 'u': o = put_num(o, va_arg(ap, u32), 10, FALSE, FALSE, width, left, zero); break;
        case 'x': o = put_num(o, va_arg(ap, u32), 16, FALSE, FALSE, width, left, zero); break;
        case 'X': case 'p': o = put_num(o, va_arg(ap, u32), 16, TRUE, FALSE, width, left, zero); break;
        case 'c': {
            if (!left) while (width-- > 1) *o++ = ' ';
            *o++ = (char)va_arg(ap, int);
            if (left) while (width-- > 1) *o++ = ' ';
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            s16 n = s ? (s16)strlen(s) : 0;
            if (!left) while (width-- > n) *o++ = ' ';
            while (s && *s) *o++ = *s++;
            if (left) while (width-- > n) *o++ = ' ';
            break;
        }
        case '%': *o++ = '%'; break;
        case 0: fmt--; break;
        default: *o++ = *fmt; break;
        }
    }
    *o = 0;
    return o - buf;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}
