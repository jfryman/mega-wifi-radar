/* src/splash.h on the 32X: the boot narration as shared state; the master draws the scene (sh/rsplash.c). */
#include "splash.h"
#include "shared.h"

#define MIN_FRAMES  75          /* each call stays on screen at least this long (START skips) */

static u32 shown_at;

static void hold(void)
{
    while (mars_vtimer - shown_at < MIN_FRAMES && !(mars_pad() & BUTTON_START)) mars_wait_frame();
}

static void copy(char *dst, const char *s, u16 n)
{
    u16 i = 0;
    if (s) for (; s[i] && i < n - 1; i++) dst[i] = s[i];
    dst[i] = 0;
}

void splash_start(enum splash_phase at)
{
    sh.sp.phase = at;
    sh.sp.finishing = 0;
    sh.sp.done = 0;
    sh.sp.detail[0] = 0;
    view_set(VIEW_SPLASH);
    shown_at = mars_vtimer;
}

void splash_phase(enum splash_phase p, const char *detail)
{
    hold();
    copy(sh.sp.detail, detail, sizeof(sh.sp.detail));
    sh.sp.phase = p;
    shown_at = mars_vtimer;
}

void splash_status(const char *s) { copy(sh.sp.status, s, sizeof(sh.sp.status)); }

void splash_finish(void)
{
    hold();
    sh.sp.finishing = 1;
    while (!sh.sp.done && !(mars_pad() & BUTTON_START)) mars_wait_frame();
    view_set(VIEW_BLANK);
}

void splash_stop(void) { view_set(VIEW_BLANK); }
