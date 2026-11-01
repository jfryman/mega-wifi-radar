/* Master SH-2: frame buffer primitives (draw.h). */
#include "draw.h"
#include "mars.h"
#include "font.h"

static u16 fb_cur;

/* CRAM takes SH-2 writes only in blanking (FBCTL PEN) on hardware; MAME takes them any time. pal_set() writes
 * this copy and fb_flip() copies the changed range in during the V-Blank it returns in. */
static u16 pal[256];
static u16 pal_lo = 256, pal_hi;

static void vblank_wait(void)
{
    while (!(MARS_FBCTL & FB_VBLK)) {}
}

static void pal_flush(void)
{
    if (pal_lo >= pal_hi) return;
    vblank_wait();
    for (u16 i = pal_lo; i < pal_hi; i++) MARS_CRAM[i] = pal[i];
    pal_lo = 256;
    pal_hi = 0;
}

/* MARS_DEBUG: the whole CRAM one colour, so the screen shows it whatever the frame buffer holds */
static void stage(u16 rgb)
{
#ifdef MARS_DEBUG
    while (MARS_FBCTL & FB_VBLK) {}
    vblank_wait();
    for (u16 i = 0; i < 256; i++) MARS_CRAM[i] = rgb;
    pal_lo = 0;                             /* the next flip puts the real palette back */
    pal_hi = 256;
#else
    (void)rgb;
#endif
}

void fb_init(void)
{
    while (!(MARS_ADAPTER & 0x8000)) {}     /* FM: the 68000 has handed us the 32X VDP */
    MARS_DISPMODE = MODE_256;               /* 224 lines; Mega Drive layer in front where it isn't backdrop */
    stage(RGB(31, 0, 0));                   /* red: display on */
    fb_cur = MARS_FBCTL & FB_FS;
    for (u16 b = 0; b < 2; b++) {
        for (u16 y = 0; y < 256; y++) MARS_FB[y] = 0x100 + (y < FB_H ? y : FB_H - 1) * 160;
        if (!b) stage(RGB(31, 31, 0));      /* yellow: line table written */
        fb_clear(0);
        if (!b) stage(RGB(31, 0, 31));      /* magenta: fill done */
        fb_flip();
        if (!b) stage(RGB(0, 31, 31));      /* cyan: first flip done */
    }
}

void fb_ints_on(void)
{
    MARS_INTMASK = 0x08;                    /* V-Int on this CPU */
    __asm__ volatile("ldc %0, sr" : : "r"(0x00));
    stage(RGB(31, 31, 31));                 /* white: interrupts on */
}

void fb_flip(void)
{
    MARS_FBCTL = fb_cur ^ 1;
    while ((MARS_FBCTL & FB_FS) == fb_cur) {}
    fb_cur ^= 1;
    pal_flush();
}

static void fill(u16 word, u16 n, u8 c)
{
    u16 v = c << 8 | c;
    while (n) {
        u16 k = 256 - (word & 0xFF);            /* the fill's address wraps within a 256-word page */
        if (k > n) k = n;
        while (MARS_FBCTL & FB_FEN) {}
        MARS_FILLEN = k - 1;
        MARS_FILADR = word;
        MARS_FILDAT = v;
        word += k;
        n -= k;
    }
    while (MARS_FBCTL & FB_FEN) {}
}

void fb_clear(u8 c) { fill(0x100, FB_H * 160, c); }

void fb_fill_rows(u16 y, u16 h, u8 c)
{
    if (y >= FB_H) return;
    if (y + h > FB_H) h = FB_H - y;
    fill(0x100 + y * 160, h * 160, c);
}

void fb_rect(s16 x, s16 y, s16 w, s16 h, u8 c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    if (w <= 0 || h <= 0) return;
    if (x == 0 && w == FB_W) { fb_fill_rows(y, h, c); return; }
    u16 v = c << 8 | c;
    for (s16 r = 0; r < h; r++) {
        volatile u8 *p = FB_PX + (y + r) * FB_W + x;
        s16 n = w;
        if ((u32)p & 1) { *p++ = c; n--; }
        volatile u16 *q = (volatile u16 *)p;
        for (; n >= 2; n -= 2) *q++ = v;
        if (n) *(volatile u8 *)q = c;
    }
}

void fb_px(s16 x, s16 y, u8 c)
{
    if ((u16)x < FB_W && (u16)y < FB_H) FB_PX[y * FB_W + x] = c;
}

void fb_hline(s16 x0, s16 x1, s16 y, u8 c)
{
    if (x0 > x1) { s16 t = x0; x0 = x1; x1 = t; }
    fb_rect(x0, y, x1 - x0 + 1, 1, c);
}

void fb_vline(s16 x, s16 y0, s16 y1, u8 c)
{
    if (y0 > y1) { s16 t = y0; y0 = y1; y1 = t; }
    if ((u16)x >= FB_W) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= FB_H) y1 = FB_H - 1;
    for (volatile u8 *p = FB_PX + y0 * FB_W + x; y0 <= y1; y0++, p += FB_W) *p = c;
}

void fb_frame(s16 x, s16 y, s16 w, s16 h, u8 c)
{
    fb_hline(x, x + w - 1, y, c);
    fb_hline(x, x + w - 1, y + h - 1, c);
    fb_vline(x, y, y + h - 1, c);
    fb_vline(x + w - 1, y, y + h - 1, c);
}

void fb_line(s16 x0, s16 y0, s16 x1, s16 y1, u8 c)
{
    s16 dx = abs(x1 - x0), dy = -abs(y1 - y0);
    s16 sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        fb_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        s16 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void plot8(s16 cx, s16 cy, s16 x, s16 y, u8 c)
{
    fb_px(cx + x, cy + y, c); fb_px(cx - x, cy + y, c);
    fb_px(cx + x, cy - y, c); fb_px(cx - x, cy - y, c);
    fb_px(cx + y, cy + x, c); fb_px(cx - y, cy + x, c);
    fb_px(cx + y, cy - x, c); fb_px(cx - y, cy - x, c);
}

void fb_dotted_circle(s16 cx, s16 cy, s16 r, u8 c, u8 every)
{
    s16 x = r, y = 0, err = 1 - r;
    u16 n = 0;
    while (x >= y) {
        if (!every || !(n++ % every)) plot8(cx, cy, x, y, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void fb_circle(s16 cx, s16 cy, s16 r, u8 c) { fb_dotted_circle(cx, cy, r, c, 0); }

static inline u8 glyph_row(u8 ch, u16 r)
{
    if (ch < 32 || ch >= 32 + FONT_NCHARS) ch = '?';
    return (u8)(font[(ch - 32) * 2 + (r >> 2)] >> ((r & 3) * 8));
}

s16 fb_textn(s16 x, s16 y, const char *s, u16 n, u8 fg, u8 bg)
{
    u16 lut[4] = { bg << 8 | bg, fg << 8 | bg, bg << 8 | fg, fg << 8 | fg };   /* bit 0 = left pixel */
    volatile u16 *base = bg ? FB_WD : FB_OVW;
    x &= ~1;
    for (; n && *s; n--, s++, x += 8) {
        if (x < 0 || x > FB_W - 8 || y < 0 || y > FB_H - 8) continue;
        volatile u16 *p = base + y * 160 + (x >> 1);
        for (u16 r = 0; r < 8; r++, p += 160) {
            u8 bits = glyph_row(*s, r);
            p[0] = lut[bits & 3];
            p[1] = lut[(bits >> 2) & 3];
            p[2] = lut[(bits >> 4) & 3];
            p[3] = lut[(bits >> 6) & 3];
        }
    }
    return x;
}

s16 fb_text(s16 x, s16 y, const char *s, u8 fg, u8 bg) { return fb_textn(x, y, s, 0xFFFF, fg, bg); }

void fb_text_big(s16 x, s16 y, const char *s, u16 scale, u8 fg, u8 shadow)
{
    for (; *s; s++, x += 8 * scale)
        for (u16 r = 0; r < 8; r++) {
            u8 bits = glyph_row(*s, r);
            for (u16 b = 0; b < 8; b++)
                if (bits & (1 << b)) {
                    if (shadow) fb_rect(x + b * scale + 1, y + r * scale + 1, scale, scale, shadow);
                    fb_rect(x + b * scale, y + r * scale, scale, scale, fg);
                }
        }
}

void pal_set(u16 i, u16 rgb)
{
    pal[i] = rgb;
    if (i < pal_lo) pal_lo = i;
    if (i >= pal_hi) pal_hi = i + 1;
}

u16 rgb_mix(u16 a, u16 b, u16 t, u16 n)
{
    if (!n) return a;
    s16 r = (a & 31) + ((s16)(b & 31) - (s16)(a & 31)) * (s16)t / (s16)n;
    s16 g = ((a >> 5) & 31) + ((s16)((b >> 5) & 31) - (s16)((a >> 5) & 31)) * (s16)t / (s16)n;
    s16 bl = ((a >> 10) & 31) + ((s16)((b >> 10) & 31) - (s16)((a >> 10) & 31)) * (s16)t / (s16)n;
    return RGB(r, g, bl);
}

void pal_ramp(u16 i, u16 n, u16 from, u16 to)
{
    for (u16 k = 0; k < n; k++) pal_set(i + k, rgb_mix(from, to, k, n > 1 ? n - 1 : 1));
}

u16 rgb_genesis(u16 c)
{
    u16 r = c & 0xF, g = (c >> 4) & 0xF, b = (c >> 8) & 0xF;
    return RGB(r * 31 / 14, g * 31 / 14, b * 31 / 14);
}
