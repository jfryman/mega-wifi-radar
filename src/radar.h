#ifndef RADAR_H
#define RADAR_H

/* The radar renderer. After radar_start() it owns the VDP and runs entirely
 * in the V-Int, so the sweep never stalls while the main loop blocks on the
 * network. The main loop talks to it only through the calls below and the
 * track table (traffic.h). Design: lode/ui/radar-renderer.md. */
#include <genesis.h>

#define PANEL_COL   28      /* window plane: cols 28..38 */
#define PANEL_W     11
#define PANEL_ROWS  28

/* Main loop, before radar_start(): VDP setup, tiles, scope, static text. */
void radar_init(void);
/* Install the V-Int renderer. From here on the main loop must not touch the VDP. */
void radar_start(void);
/* Remove it (setup screen); radar_init() + radar_start() to come back. */
void radar_stop(void);

/* ---- main loop -> renderer (safe to call any time after radar_init) ---- */
void radar_panel(u16 row, const char *text);    /* set a side-panel row */
void radar_rescale(void);       /* range changed: rescale trails, tags, labels */
void radar_retheme(void);       /* settings.theme changed */
void radar_retag(void);         /* tag mode or selection changed */
void radar_pan(s16 dx, s16 dy); /* nudge the view (clamped) */
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
