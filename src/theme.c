#include "theme.h"

/* VDP colours are 0x0BGR, 3 bits per channel (even nibble values). */
const struct theme themes[THEME_COUNT] = {
    [THEME_CLASSIC] = {
        .name = "CLASSIC",
        .pal = {
            /* PAL0: bg, ring, glow 2..11 (set at runtime), trail, -, -, white text */
            { 0x000, 0x040, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0A0, 0x000, 0x000, 0xEEE },
            /* PAL1: -, blip levels (bright..dim), amber select, grey stale, ..., green text */
            { 0x000, 0x6E6, 0x0C0, 0x080, 0x040, 0x0AE, 0x666, 0, 0, 0, 0, 0, 0, 0, 0, 0x0E0 },
            /* PAL2: cyan text */
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xEE0 },
            /* PAL3: red blip levels + red text */
            { 0x000, 0x66E, 0x00E, 0x00A, 0x006, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x22E },
        },
        .glow = { 0x8E8, 0x0E0, 0x0C0, 0x0A0, 0x080, 0x060, 0x060, 0x040, 0x020, 0x020 },
        .sweep = TRUE,
        .blip_fade = TRUE,
    },
    [THEME_STARS] = {
        .name = "STARS",
        .pal = {
            /* PAL0: black bg, blue-grey rings, no glow, grey trail, white text */
            { 0x000, 0x844, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x888, 0x000, 0x000, 0xEEE },
            /* PAL1: white blips, amber select, grey stale, white-ish text */
            { 0x000, 0xEEE, 0xEEE, 0xEEE, 0xEEE, 0x0AE, 0x666, 0, 0, 0, 0, 0, 0, 0, 0, 0xCCC },
            /* PAL2: cyan text */
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xEE4 },
            /* PAL3: red */
            { 0x000, 0x22E, 0x22E, 0x22E, 0x22E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x22E },
        },
        .glow = { 0 },
        .sweep = FALSE,
        .blip_fade = FALSE,
    },
};
