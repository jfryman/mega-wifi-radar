#ifndef RADAR_H
#define RADAR_H

/* The radar renderer. After radar_start() it owns the VDP and runs entirely
 * in the V-Int, so the sweep never stalls while the main loop blocks on the
 * network. The main loop talks to it only through the calls below and the
 * track table (traffic.h). Design: lode/ui/radar-renderer.md. */
#include <genesis.h>
#include "arframe.h"

#define PANEL_COL   28      /* window plane: cols 28..38 */
#define PANEL_W     11
#define PANEL_ROWS  28
#define POPUP_COLS  28      /* pop-up: window cols 0..27 (text in 1..26 stays inside the TV's crop) */

/* Main loop, before radar_start(): VDP setup, tiles, scope, static text. */
void radar_init(void);
/* Install the V-Int renderer. From here on the main loop must not touch the VDP. */
void radar_start(void);
/* Remove it (setup screen); radar_init() + radar_start() to come back. */
void radar_stop(void);

/* ---- main loop -> renderer (safe to call any time after radar_init) ---- */
void radar_panel(u16 row, const char *text);    /* set a side-panel row (PAL0) */
void radar_panel_pal(u16 row, const char *text, u16 pal);   /* ... in a text palette; no-op if unchanged */
void radar_popup_row(u16 row, const char *text, u16 pal);  /* pop-up row, padded to POPUP_COLS */
void radar_popup(bool on);      /* show / hide the pop-up (shows once its rows are written) */
/* The flyover notice box (lode/ui/notices.md): grows open over the scope's centre and shows text (3 lines of
 * AR_NOTICE_COLS, then a hint line, with a palette each); called again while open, it just rewrites the text.
 * close() shrinks it shut (now: without the animation's pauses, e.g. under the pop-up). */
void radar_box_open(const char *text, const u8 pal[4]);
void radar_box_pal(u16 row, u16 pal);   /* recolour one text row (the headline's blink) */
void radar_box_close(bool now);
bool radar_box_up(void);                /* drawn at all (opening, open or closing) */
/* Weather (lode/ui/weather.md): AR_WX_N rows of AR_WX_ROW bytes as struct ar_wx sends them, or NULL for none.
 * Drawn a few rows a frame under the tags; a zoom wipes it (it was sampled at the old range). */
void radar_wx(const u8 *cells);
extern const u32 radar_wx_tiles[3][8];  /* the weather stipple, light .. heavy (setup's guide shows them too) */
/* Video map (lode/ui/video-maps.md), main loop. radar_map_begin() removes the map (a drawn one is cleared from the
 * scope, trail dots redrawn) and lends its buffer (*size bytes) for a struct ar_map + segments to be fetched into;
 * radar_map_end(TRUE) draws it, a few lines a frame; radar_map_end(FALSE) leaves none. A zoom wipes it (it was
 * projected at the old range). */
u8 *radar_map_begin(u16 *size);
void radar_map_end(bool ok);
void radar_rescale(void);       /* range changed: rescale trails, tags, labels */
void radar_retheme(void);       /* settings.theme changed */
void radar_retag(void);         /* tag mode or selection changed */
void radar_select(s16 track);   /* -1 = none */
s16 radar_selected(void);

/* Sweep passes north once per rotation: the main loop polls the feed when
 * this counter changes. */
u16 radar_rotations(void);
/* Buttons as seen by the V-Int (JOY_update runs there). */
u16 radar_joy(void);
/* Debug: worst-case scanlines used by the V-Int handler (reset by reader). */
extern volatile u16 vint_lines_max;
extern volatile u32 vint_lines_sum;
extern volatile u32 prof_sum[8];
extern volatile u16 main_beat;
extern volatile u16 late_frames;

#endif
