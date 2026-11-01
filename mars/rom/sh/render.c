/* Master SH-2: the renderer (lode/mars/summary.md). It draws the view the slave asks for, one frame at a time,
 * into the back buffer, then flips; its V-Int (sh/start.s) counts frames for both SH-2s (mars_vtimer). */
#include "render.h"
#include "draw.h"
#include "mars.h"
#include "shared.h"

void text_palette(const u16 rgb[4])
{
    for (u16 i = 0; i < 4; i++) pal_set(C_TXT(i), rgb[i]);
    pal_set(C_SHADOW, RGB(0, 0, 0));
    pal_set(C_DIM, RGB(12, 12, 12));
}

static void blank_enter(void)
{
    pal_set(0, 0);
}

void m_main(void)
{
    u32 seen = 0, win = 0;
    u16 frames = 0;
    u8 view = VIEW_BLANK;

    fb_init();
    MARS_INTMASK = 0x08;                    /* V-Int on this CPU */
    __asm__ volatile("ldc %0, sr" : : "r"(0x00));
    for (;;) {
        if (sh.view_seq != seen) {
            seen = sh.view_seq;
            view = sh.view;
            switch (view) {
            case VIEW_SPLASH: rsplash_enter(); break;
            case VIEW_RADAR: rradar_enter(); break;
            case VIEW_TEXT: rtext_enter(); break;
            case VIEW_COCKPIT: rvox_enter(); break;
            default: blank_enter(); break;
            }
        }
        switch (view) {
        case VIEW_SPLASH: rsplash_frame(); break;
        case VIEW_RADAR: rradar_frame(); break;
        case VIEW_TEXT: rtext_frame(); break;
        case VIEW_COCKPIT: rvox_frame(); break;
        default: fb_clear(0); break;
        }
#ifdef MARS_DEBUG
        {
            char b[48];
            sprintf(b, "V%lu W%u C%u/%u L%u %04X %04X", mars_vtimer, sh.view, sh.dbg[0], sh.dbg[1], sh.dbg[2],
                    MARS_COMM(0), MARS_COMM(8));
            sprintf(b + strlen(b), " %04X", MARS_COMM(14));
            fb_text(0, 216, b, C_TXT(0), 1);
        }
#endif
        fb_flip();
        sh.view_ack = seen;                 /* the slave's view_set() returns once a frame of it is up */
        if (++frames == 32) {
            u32 now = mars_vtimer;
            sh.fps10 = now != win ? (u16)(600UL * frames / (now - win)) : 0;
            win = now;
            frames = 0;
        }
    }
}
