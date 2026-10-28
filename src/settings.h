#ifndef SETTINGS_H
#define SETTINGS_H

/* User-facing settings. Defaults come from devcfg; the config screen edits
 * them and they persist in ESP32 user flash (lode/ui/config-screen.md). */
#include <genesis.h>

#define RANGE_COUNT 5
extern const u16 range_steps[RANGE_COUNT];      /* 5, 10, 25, 50, 100 nm */

enum tag_mode { TAGS_ALL, TAGS_SELECTED, TAGS_OFF, TAGS_MODES };

struct settings {
    u8 theme;           /* THEME_CLASSIC / THEME_STARS */
    u8 range_idx;       /* index into range_steps */
    u8 tag_mode;
};

extern struct settings settings;

/* Defaults (devcfg range snapped to the nearest step), then any saved copy. */
void settings_init(void);
static inline u16 settings_range_nm(void) { return range_steps[settings.range_idx]; }

#endif
