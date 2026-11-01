/* Master SH-2: the full-screen text view (setup, keyboard, controls page): sh.text over a 32X backdrop. */
#include "render.h"
#include "draw.h"
#include "mars.h"
#include "shared.h"

#define C_BG0   1       /* 1..56: the backdrop gradient, top to bottom */
#define C_BAND  60
#define C_EDGE  61
#define C_RED   62

void rtext_enter(void)
{
    static const u16 txt[4] = { RGB(30, 30, 30), RGB(12, 28, 14), RGB(8, 26, 31), RGB(31, 10, 8) };
    pal_set(0, RGB(0, 0, 2));
    pal_ramp(C_BG0, 28, RGB(2, 3, 10), RGB(1, 1, 5));
    pal_ramp(C_BG0 + 28, 28, RGB(1, 1, 5), RGB(0, 0, 2));
    pal_set(C_BAND, RGB(4, 6, 16));
    pal_set(C_EDGE, RGB(10, 14, 28));
    pal_set(C_RED, RGB(26, 3, 3));
    text_palette(txt);
}

void rtext_frame(void)
{
    for (u16 b = 0; b < 56; b++) fb_fill_rows(b * 4, 4, C_BG0 + b);
    fb_rect(0, 4, FB_W, 14, C_BAND);                /* title band behind row 1 */
    fb_hline(0, FB_W - 1, 18, C_EDGE);
    fb_rect(0, 199, FB_W, 10, C_BAND);              /* status band behind row 25 */
    fb_hline(0, FB_W - 1, 198, C_EDGE);
    fb_rect(FB_W - 40, 6, 32, 10, C_RED);           /* the 32X badge */
    fb_text(FB_W - 36, 7, "32X", C_TXT(0), 0);
    for (u16 r = 0; r < TEXT_ROWS; r++) {
        const char *row = sh.text[r];
        const u8 *pal = sh.text_pal[r];
        for (u16 c = 0; c < TEXT_COLS; c++) {
            if (row[c] == ' ' || !row[c]) continue;
            u16 n = 1;
            while (c + n < TEXT_COLS && row[c + n] != ' ' && pal[c + n] == pal[c]) n++;
            fb_textn(c * 8, r * 8, row + c, n, C_TXT(pal[c] & 3), 0);
            c += n - 1;
        }
    }
}
