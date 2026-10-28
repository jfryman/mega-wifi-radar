#include "ui.h"
#include "settings.h"
#include "theme.h"

#define REPEAT_DELAY 16
#define REPEAT_RATE  4
#define STATUS_ROW   25

static u16 prev, held;

void ui_wait_release(void)
{
    do SYS_doVBlankProcess(); while (JOY_readJoypad(JOY_1) & BUTTON_ALL);
    prev = 0;
    held = 0;
}

u16 ui_pad(void)
{
    u16 j, pressed;

    SYS_doVBlankProcess();
    j = JOY_readJoypad(JOY_1);
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
    const struct theme *t = &themes[settings.theme];

    VDP_setWindowHPos(FALSE, 0);
    VDP_setWindowVPos(FALSE, 0);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0);
    VDP_setVerticalScroll(BG_B, 0);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_resetSprites();
    VDP_updateSprites(1, CPU);
    for (u16 p = 0; p < 4; p++) PAL_setColors(p * 16, t->pal[p], 16, CPU);
}

void ui_text(u16 pal, u16 x, u16 y, const char *s)
{
    VDP_setTextPalette(pal);
    VDP_drawText(s, x, y);
    VDP_setTextPalette(PAL0);
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
