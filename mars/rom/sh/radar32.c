/* src/radar.h on the 32X, slave side: the app's calls only change shared state (sh, the track table); the
 * master SH-2 draws it (sh/rradar.c), as the Genesis V-Int did. */
#include "radar.h"
#include "shared.h"
#include "traffic.h"

_Static_assert(OV_TRACKS == 22720, "sh/sh.ld places overlay_ram at mars_tracks - OV_TRACKS");

struct track mars_tracks[TR_MAX] SHARED;
volatile u16 vint_lines_max;
volatile u32 vint_lines_sum;
volatile u32 prof_sum[8];
volatile u16 main_beat;
volatile u16 late_frames;

void radar_init(void)
{
    lock_take(1);
    memset(mars_tracks, 0, sizeof(mars_tracks));
    memset((void *)tr_state, 0, TR_MAX);
    for (u16 i = 0; i < TR_MAX; i++) mars_tracks[i].tag_x = mars_tracks[i].tag_y = -1;
    memset(sh.panel, 0, sizeof(sh.panel));
    memset(sh.panel_pal, 0, sizeof(sh.panel_pal));
    memset(sh.popup, ' ', sizeof(sh.popup));
    sh.popup_on = 0;
    sh.selected = -1;
    sh.rotations = 0;
    sh.req_rescale = sh.req_theme = sh.req_retag = 0;
    lock_give(1);
}

void radar_start(void) { view_set(VIEW_RADAR); }
void radar_stop(void) { }

void radar_panel_pal(u16 row, const char *text, u16 pal)
{
    char buf[PANEL_W + 1];

    if (row >= PANEL_ROWS) return;
    memset(buf, 0, sizeof(buf));
    for (u16 i = 0; i < PANEL_W && text[i]; i++) buf[i] = text[i];
    memcpy(sh.panel[row], buf, sizeof(buf));
    sh.panel_pal[row] = pal;
}

void radar_panel(u16 row, const char *text) { radar_panel_pal(row, text, PAL0); }

void radar_popup_row(u16 row, const char *text, u16 pal)
{
    u16 i = 0;

    if (row >= PANEL_ROWS) return;
    for (; i < POPUP_COLS && text[i]; i++) sh.popup[row][i] = text[i];
    for (; i < POPUP_COLS; i++) sh.popup[row][i] = ' ';
    sh.popup_pal[row] = pal;
}

void radar_popup(bool on) { sh.popup_on = on; }
bool radar_popup_shown(void) { return sh.popup_on; }
void radar_rescale(void) { sh.req_rescale = 1; }
void radar_retheme(void) { sh.req_theme = 1; sh.req_retag = 1; }
void radar_retag(void) { sh.req_retag = 1; }
void radar_select(s16 track) { sh.selected = track; }
s16 radar_selected(void) { return sh.selected; }
u16 radar_rotations(void) { return sh.rotations; }
u16 radar_joy(void) { return mars_pad(); }
