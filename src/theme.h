#ifndef THEME_H
#define THEME_H

/* Themes are data: four palettes + flags (lode/ui/screen-layout.md). */
#include <genesis.h>

/* PAL0: scope (plane B) + white text */
#define C_BG        0
#define C_RING      1
#define C_GLOW0     2       /* glow slots 2..11 (lode/ui/sweep.md) */
#define GLOW_SLOTS  10
#define C_TRAIL     12
/* PAL1: blips (1..4 = brightness levels, 5 = selection box, 6 = stale) */
#define C_BLIP0     1
#define C_SELECT    5
#define C_STALE     6
/* every palette: index 15 = text colour (the SGDK font draws with 15) */

/* Text palettes: tags pick one by altitude band. */
#define PAL_TEXT_HIGH   PAL0    /* white: >= FL180, panel */
#define PAL_TEXT_LOW    PAL1    /* green: < FL100, ground */
#define PAL_TEXT_MID    PAL2    /* cyan: FL100..FL179 */
#define PAL_TEXT_EMERG  PAL3    /* red: emergency (blips too) */

enum { THEME_CLASSIC, THEME_STARS, THEME_COUNT };

struct theme {
    const char *name;
    u16 pal[4][16];
    u16 glow[GLOW_SLOTS];   /* afterglow ramp, newest first */
    bool sweep;             /* draw the thin-line sweep */
    bool blip_fade;         /* blips fade after the sweep passes */
};

extern const struct theme themes[THEME_COUNT];

#endif
