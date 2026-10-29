#ifndef TRAFFIC_H
#define TRAFFIC_H

/* Tracked aircraft. The main loop ingests frames (writing `next`); the VInt
 * applies `next` when the sweep crosses its bearing (lode/ui/screen-layout.md).
 * Fields marked [main] are written by the main loop under SYS_disableInts();
 * [vint] fields belong to the renderer. */
#include <genesis.h>
#include "arframe.h"

#define TR_MAX      AR_MAX_RECS
#define TRAIL_N     5
#define TAG_W       8

struct track {
    u8 used;                    /* [main] slot in use */
    u8 drop;                    /* [main] gone from the feed: VInt frees it */
    u8 has_next;                /* [main] sets, [vint] clears on apply */
    u8 next_brg;                /* [main] sweep step of next position */
    u8 missed;                  /* [main] polls in a row without this aircraft */
    u8 shown;                   /* [vint] cur is valid and displayed */
    u8 hit_step;                /* [vint] sweep step of the last apply */
    u8 dirty;                   /* [vint] tag/trail need redrawing */
    struct ar_rec next;         /* [main] latest data */
    char next_tag[2][TAG_W];    /* [main] tag text for next (callsign / "alt gs") */
    u8 next_pal;                /* [main] tag palette for next (altitude band) */
    char tag[2][TAG_W];         /* [vint] tag text for cur */
    u8 tag_pal;
    struct ar_rec cur;          /* [vint] displayed data */
    s16 trail_x[TRAIL_N];       /* [vint] past positions, [0] newest (1/32 nm) */
    s16 trail_y[TRAIL_N];
    u8 trail_n;
    s16 bx, by;                 /* [vint] cur position in disc-block px */
    u16 spr_y, spr_x;           /* [vint] sprite words (VDP +128 offsets) */
    u16 spr_attr;               /* [vint] blip attr with brightness level 0 */
    u8 vis;                     /* [vint] cur is inside the disc at this range */
    s8 tag_x, tag_y;            /* [vint] tag cell on plane A, -1 = none */
    s16 dot_x[TRAIL_N], dot_y[TRAIL_N];  /* [vint] block px of trail[n]'s dot, NO_DOT if none */
};

extern struct track tracks[TR_MAX];

/* Compact mirror of the flags the V-Int scans every frame. */
#define TS_USED     0x01
#define TS_DROP     0x02
#define TS_NEXT     0x04
extern volatile u8 tr_state[TR_MAX];
extern volatile u8 tr_brg[TR_MAX];

/* Main loop: merge a validated frame into the track table. */
void traffic_ingest(const struct ar_hdr *h);
/* Tracks currently in use (main loop). */
u16 traffic_count(void);

#endif
