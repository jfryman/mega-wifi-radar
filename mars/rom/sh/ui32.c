/* src/ui.h on the 32X: setup's text screens go into sh.text, which the master draws (sh/rtext.c). */
#include "ui.h"
#include "shared.h"

#define REPEAT_DELAY 16
#define REPEAT_RATE  4
#define STATUS_ROW   25

static u16 prev, held;

void ui_wait_release(void)
{
    do mars_wait_frame(); while (mars_pad() & BUTTON_ALL);
    prev = 0;
    held = 0;
}

u16 ui_pad(void)
{
    u16 j, pressed;

    mars_wait_frame();
    j = mars_pad();
    pressed = j & ~prev;
    if (j & BUTTON_DIR & prev) {
        if (++held >= REPEAT_DELAY && !((held - REPEAT_DELAY) % REPEAT_RATE)) pressed |= j & BUTTON_DIR;
    } else {
        held = 0;
    }
    prev = j;
    return pressed;
}

void ui_clear(void)
{
    memset(sh.text, ' ', sizeof(sh.text));
    memset(sh.text_pal, 0, sizeof(sh.text_pal));
    if (sh.view != VIEW_TEXT) view_set(VIEW_TEXT);
}

void ui_text(u16 pal, u16 x, u16 y, const char *s)
{
    if (y >= TEXT_ROWS) return;
    for (; *s && x < TEXT_COLS; s++, x++) {
        sh.text[y][x] = *s;
        sh.text_pal[y][x] = pal;
    }
}

void ui_field(u16 pal, u16 x, u16 y, u16 w, const char *s)
{
    char buf[41];
    u16 i = 0;

    for (; i < w && i < 40 && s[i]; i++) buf[i] = s[i];
    for (; i < w && i < 40; i++) buf[i] = ' ';
    buf[i] = 0;
    ui_text(pal, x, y, buf);
}

void ui_status(u16 pal, const char *s)
{
    ui_field(pal, 1, STATUS_ROW, 38, s);
}

void mars_hard_reset(void)
{
    ui_status(PAL3, "DONE: POWER THE CONSOLE OFF AND ON");
    for (;;) mars_wait_frame();
}
