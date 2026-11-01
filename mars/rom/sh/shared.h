/* State the two SH-2s share (lode/mars/summary.md): in section .shared, linked at the cache-through alias, so
 * every access goes to SDRAM. The slave (the app) writes requests and text; the master (the renderer) draws
 * from them. Fields marked [m] are written only by the master. */
#ifndef SHARED_H
#define SHARED_H
#include "genesis.h"
#include "arframe.h"
#include "radar.h"

#define SHARED          __attribute__((section(".shared")))
#define TEXT_COLS       40
#define TEXT_ROWS       28

enum view {
    VIEW_BLANK, VIEW_SPLASH, VIEW_RADAR, VIEW_TEXT, VIEW_COCKPIT,
};

/* Splash (sh/splash32.c -> sh/rsplash.c) */
struct sp_shared {
    u8 phase;                   /* enum splash_phase */
    u8 finishing;               /* splash_finish(): depart and fade */
    u8 done;                    /* [m] the departure is over */
    char detail[48];
    char status[48];
};

/* Cockpit (sh/cockpit32.c -> sh/rvox.c). The slave posts fixes; the master flies the camera between them. */
struct ck_fix {
    s32 x, y, z;                /* where the aircraft is now: 16.16 cells / levels */
    s32 vx, vy, vz;             /* per frame, 16.16 */
    u16 heading;                /* target, 65536 per turn */
    s16 hd_rate;                /* per frame */
    s16 bank;                   /* degrees */
    s16 horizon;                /* target row */
    u8 snap;                    /* jump there instead of blending */
};
struct ck_cam {                 /* [m] */
    s32 x, y, z;                /* 16.16 */
    u16 heading;
    s16 bank;
    s16 horizon;
};
struct ck_shared {
    u16 fix_seq;                /* bumped by the slave after writing fix */
    struct ck_fix fix;
    struct ck_cam cam;          /* [m] the camera as flown, for the slave's streaming */
    u16 terrain_gen;            /* bumped after the maps or palette change: the master purges its cache */
    u16 pal[16];                /* the patch's palette (Genesis 0x0BGR) */
    u16 k_q8;                   /* rows per level at 1 cell, Q8 */
    u8 hmax;
    u8 status;                  /* LIVE / NO SIGNAL / TERRAIN... / SIGNAL LOST */
    u16 fps10;                  /* [m] */
    char hud[4][40];            /* HUD lines */
    char msg[40];               /* exit message, over the credit line */
};

struct shared {
    u32 view_seq;               /* bumped with every view change */
    u32 view_ack;               /* [m] the view_seq it has set up and drawn */
    u8 view;                    /* enum view */
    /* radar */
    s16 selected;
    u16 rotations;              /* [m] the sweep passes north */
    u8 req_rescale, req_theme, req_retag;
    u8 popup_on;
    char panel[PANEL_ROWS][PANEL_W + 1];
    u8 panel_pal[PANEL_ROWS];
    char popup[PANEL_ROWS][POPUP_COLS];
    u8 popup_pal[PANEL_ROWS];
    /* full-screen text (setup, keyboard, controls) */
    char text[TEXT_ROWS][TEXT_COLS];
    u8 text_pal[TEXT_ROWS][TEXT_COLS];
    struct sp_shared sp;
    struct ck_shared ck;
    u16 rom_checksum;
    u8 fake;                    /* the 68000 serves the recorded demo */
    u16 fps10;                  /* [m] radar frames per second x 10 */
    u16 dbg[4];                 /* MARS_DEBUG: last RPC, its stage */
};

extern volatile struct shared sh;

/* Peterson's lock between the master (cpu 0) and the slave (cpu 1), on cache-through memory. */
void lock_take(u8 cpu);
void lock_give(u8 cpu);

void view_set(enum view v);     /* slave: switch the master's view, wait until it's drawn once */

#endif
