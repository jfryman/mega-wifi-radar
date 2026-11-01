/* Master SH-2: drawing into the 32X frame buffer (256 colours, 320 x 224). Everything draws into the back
 * buffer, which the 32X always maps at $24000000. lode/mars/renderer.md. */
#ifndef DRAW_H
#define DRAW_H
#include "genesis.h"

#define FB_W        320
#define FB_H        224
#define FB_PX       ((volatile u8 *)0x24000200)     /* pixel (x, y) at y * 320 + x */
#define FB_WD       ((volatile u16 *)0x24000200)    /* two pixels: (2i, y) at y * 160 + i */
#define FB_OV       ((volatile u8 *)0x24020200)     /* overwrite image: 0 bytes leave the pixel alone */
#define FB_OVW      ((volatile u16 *)0x24020200)

void fb_init(void);                         /* 256-colour mode, line tables in both buffers */
void fb_flip(void);                         /* show the back buffer at the next vblank, wait for it, load the palette */
void fb_ints_on(void);                      /* V-Int on for the master (sh/start.s m_vint) */
void fb_clear(u8 c);                        /* whole screen, by the 32X's fill hardware */
void fb_fill_rows(u16 y, u16 h, u8 c);      /* full-width rows, by the fill hardware */
void fb_rect(s16 x, s16 y, s16 w, s16 h, u8 c);
void fb_frame(s16 x, s16 y, s16 w, s16 h, u8 c);   /* 1-px outline */
void fb_hline(s16 x0, s16 x1, s16 y, u8 c);
void fb_vline(s16 x, s16 y0, s16 y1, u8 c);
void fb_px(s16 x, s16 y, u8 c);
void fb_line(s16 x0, s16 y0, s16 x1, s16 y1, u8 c);
void fb_circle(s16 cx, s16 cy, s16 r, u8 c);
void fb_dotted_circle(s16 cx, s16 cy, s16 r, u8 c, u8 every);
/* 8x8 text at (x, y), x even; bg 0 = transparent. Returns the x after it. */
s16 fb_text(s16 x, s16 y, const char *s, u8 fg, u8 bg);
s16 fb_textn(s16 x, s16 y, const char *s, u16 n, u8 fg, u8 bg);
/* scaled, transparent, with an optional 1-px drop shadow (shadow 0 = none) */
void fb_text_big(s16 x, s16 y, const char *s, u16 scale, u8 fg, u8 shadow);
void pal_set(u16 i, u16 rgb);               /* 5:5:5 */
void pal_ramp(u16 i, u16 n, u16 from, u16 to);  /* n entries from i, blending two 5:5:5 colours */
u16 rgb_mix(u16 a, u16 b, u16 t, u16 n);    /* a + (b - a) * t / n */
u16 rgb_genesis(u16 c);                     /* 0x0BGR -> 5:5:5 */

#endif
