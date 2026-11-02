#include "theme.h"

/* VDP colours are 0x0BGR, 3 bits per channel (even nibble values). */
const struct theme themes[THEME_COUNT] = {
    [THEME_CLASSIC] = {
        .name = "CLASSIC",
        .pal = {
            /* PAL0: bg, ring, glow 2..11 (set at runtime), trail, weather, -, white text */
            { 0x000, 0x040, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0A0, 0x420, 0x000, 0xEEE },
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
            { 0x000, 0x844, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x888, 0x622, 0x000, 0xEEE },
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
        .leader = TRUE,
    },
    [THEME_AMBER] = {
        .name = "AMBER",
        .pal = {
            /* amber phosphor: orange sweep, cream/amber/orange tags, red emergencies */
            { 0x000, 0x024, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x06C, 0x222, 0x000, 0x8CE },
            { 0x000, 0x8CE, 0x0AE, 0x06A, 0x036, 0xEEE, 0x444, 0, 0, 0, 0, 0, 0, 0, 0, 0x08E },
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0CE },
            { 0x000, 0x66E, 0x00E, 0x00A, 0x006, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x22E },
        },
        .glow = { 0x8CE, 0x08E, 0x06C, 0x04A, 0x048, 0x026, 0x026, 0x024, 0x002, 0x002 },
        .sweep = TRUE,
        .blip_fade = TRUE,
    },
    [THEME_NIGHT] = {
        .name = "NIGHT",
        .pal = {
            /* dim reds for a dark room; tags shift red -> orange with altitude, emergencies pale pink */
            { 0x000, 0x004, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x006, 0x202, 0x000, 0x28E },
            { 0x000, 0x00E, 0x00A, 0x006, 0x004, 0x46E, 0x224, 0, 0, 0, 0, 0, 0, 0, 0, 0x00C },
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04E },
            { 0x000, 0xCCE, 0xAAE, 0x88C, 0x668, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xCCE },
        },
        .glow = { 0x00C, 0x00A, 0x008, 0x006, 0x006, 0x004, 0x004, 0x002, 0x002, 0x002 },
        .sweep = TRUE,
        .blip_fade = TRUE,
    },
    [THEME_MARINE] = {
        .name = "MARINE",
        .pal = {
            /* ship's radar: yellow-orange targets on navy, steel-blue rings */
            { 0x200, 0x642, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08A, 0x420, 0x200, 0xEEE },
            { 0x000, 0x8EE, 0x0CE, 0x08A, 0x246, 0xEE0, 0x644, 0, 0, 0, 0, 0, 0, 0, 0x200, 0x0CE },
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x200, 0xEC4 },
            { 0x000, 0x66E, 0x00E, 0x00A, 0x226, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x200, 0x22E },
        },
        .glow = { 0x6EE, 0x0CE, 0x0AC, 0x08A, 0x268, 0x246, 0x244, 0x422, 0x420, 0x200 },
        .sweep = TRUE,
        .blip_fade = TRUE,
    },
    [THEME_ICE] = {
        .name = "ICE",
        .pal = {
            /* blue-white phosphor */
            { 0x000, 0x642, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xA86, 0x240, 0x000, 0xEEE },
            { 0x000, 0xEEE, 0xECA, 0xA86, 0x642, 0x0AE, 0x666, 0, 0, 0, 0, 0, 0, 0, 0, 0xE86 },
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xEE6 },
            { 0x000, 0x66E, 0x00E, 0x00A, 0x006, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x44E },
        },
        .glow = { 0xEEE, 0xEEC, 0xEC8, 0xCA6, 0xA84, 0x864, 0x642, 0x642, 0x420, 0x420 },
        .sweep = TRUE,
        .blip_fade = TRUE,
    },
    [THEME_POCKET] = {
        .name = "POCKET",
        .pal = {
            /* handheld LCD: dark olive on pea green, brown emergencies; no sweep, an LCD doesn't glow */
            { 0x0A8, 0x086, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x262, 0x086, 0x0A8, 0x020 },
            { 0x000, 0x020, 0x020, 0x020, 0x020, 0x024, 0x086, 0, 0, 0, 0, 0, 0, 0, 0x0A8, 0x042 },
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0A8, 0x240 },
            { 0x000, 0x026, 0x026, 0x026, 0x026, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0A8, 0x026 },
        },
        .glow = { 0 },
        .sweep = FALSE,
        .blip_fade = FALSE,
    },
    [THEME_SUNSET] = {
        .name = "SUNSET",
        .pal = {
            /* synthwave: cyan sweep over purple, magenta rings, pink trails */
            { 0x402, 0x80A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xA4C, 0x624, 0x402, 0xEEE },
            { 0x000, 0xEEA, 0xEE4, 0xCA2, 0x864, 0x0EE, 0x846, 0, 0, 0, 0, 0, 0, 0, 0x402, 0x0AE },
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x402, 0xEE2 },
            { 0x000, 0x66E, 0x00E, 0x00A, 0x006, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x402, 0x22E },
        },
        .glow = { 0xEEA, 0xEE4, 0xCC2, 0xA84, 0xA66, 0x848, 0x82A, 0x62A, 0x604, 0x402 },
        .sweep = TRUE,
        .blip_fade = TRUE,
    },
    [THEME_ERAM] = {
        .name = "ERAM",
        .pal = {
            /* en-route centre display: no sweep, grey-green maps, green targets, pale data blocks */
            { 0x000, 0x464, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x686, 0x420, 0x000, 0xCEC },
            { 0x000, 0x8E8, 0x8E8, 0x8E8, 0x8E8, 0x0CE, 0x444, 0, 0, 0, 0, 0, 0, 0, 0, 0x6C6 },
            { 0x000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xCC8 },
            { 0x000, 0x22E, 0x22E, 0x22E, 0x22E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x22E },
        },
        .glow = { 0 },
        .sweep = FALSE,
        .blip_fade = FALSE,
        .leader = TRUE,
    },
};
