#include "keyboard.h"
#include "theme.h"
#include "ui.h"

#define KB_COLS     13
#define KB_ROWS     8           /* 7 character rows + 1 action row */
#define KB_X        7
#define KB_Y        9
#define EDIT_Y      5

static const char *const keys[KB_ROWS - 1] = {
    "ABCDEFGHIJKLM",
    "NOPQRSTUVWXYZ",
    "abcdefghijklm",
    "nopqrstuvwxyz",
    "0123456789.-_",
    "!@#$%&*()+=:/",
    "?,;'\"~<>[]{}^",
};
enum { ACT_SPACE, ACT_DEL, ACT_OK, ACT_CANCEL, ACT_COUNT };
static const char *const actions[ACT_COUNT] = { "SPACE", "DEL", "OK", "CANCEL" };
static const u8 action_x[ACT_COUNT] = { 0, 7, 12, 16 };

static void draw_edit(const char *buf, u16 max)
{
    char line[41];
    u16 n = strlen(buf), off = n > 36 ? n - 36 : 0, i = 0;

    while (buf[off + i]) { line[i] = buf[off + i]; i++; }     /* last 36 chars */
    line[i++] = '_';
    line[i] = 0;
    ui_field(PAL0, 2, EDIT_Y, 38, line);
    sprintf(line, "%u/%u", n, max);
    ui_field(PAL1, 2, EDIT_Y + 1, 10, line);
}

static void draw_keys(u16 cx, u16 cy)
{
    char s[2] = { 0, 0 };

    for (u16 r = 0; r < KB_ROWS - 1; r++)
        for (u16 c = 0; c < KB_COLS; c++) {
            s[0] = keys[r][c];
            ui_text((r == cy && c == cx) ? PAL3 : PAL1, KB_X + c * 2, KB_Y + r, s);
        }
    for (u16 a = 0; a < ACT_COUNT; a++)
        ui_text((cy == KB_ROWS - 1 && cx == a) ? PAL3 : PAL2, KB_X + action_x[a], KB_Y + KB_ROWS, actions[a]);
}

bool kb_edit(const char *title, char *buf, u16 max)
{
    char orig[65];
    u16 cx = 0, cy = 0;

    strncpy(orig, buf, sizeof(orig) - 1);
    orig[sizeof(orig) - 1] = 0;
    ui_clear();
    ui_text(PAL0, 2, 2, title);
    ui_text(PAL1, 2, 22, "A TYPE  B DEL  START OK");
    draw_edit(buf, max);
    draw_keys(cx, cy);

    while (TRUE) {
        u16 p = ui_pad();
        u16 n = strlen(buf);
        bool redraw = FALSE;

        if (p & BUTTON_UP) { cy = cy ? cy - 1 : KB_ROWS - 1; redraw = TRUE; }
        if (p & BUTTON_DOWN) { cy = (cy + 1) % KB_ROWS; redraw = TRUE; }
        u16 w = cy == KB_ROWS - 1 ? ACT_COUNT : KB_COLS;
        if (cx >= w) cx = w - 1;
        if (p & BUTTON_LEFT) { cx = cx ? cx - 1 : w - 1; redraw = TRUE; }
        if (p & BUTTON_RIGHT) { cx = (cx + 1) % w; redraw = TRUE; }
        if (p & BUTTON_B && n) buf[n - 1] = 0;
        if (p & BUTTON_START) return TRUE;
        if (p & BUTTON_A) {
            if (cy < KB_ROWS - 1) {
                if (n < max) { buf[n] = keys[cy][cx]; buf[n + 1] = 0; }
            } else if (cx == ACT_SPACE) {
                if (n < max) { buf[n] = ' '; buf[n + 1] = 0; }
            } else if (cx == ACT_DEL) {
                if (n) buf[n - 1] = 0;
            } else if (cx == ACT_OK) {
                return TRUE;
            } else {
                strcpy(buf, orig);
                return FALSE;
            }
        }
        if (p & (BUTTON_A | BUTTON_B)) draw_edit(buf, max);
        if (redraw) draw_keys(cx, cy);
    }
}
