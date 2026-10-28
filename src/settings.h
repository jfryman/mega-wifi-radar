#ifndef SETTINGS_H
#define SETTINGS_H

/* User settings: defaults from devcfg, edited on the setup screen, saved in
 * ESP32 user flash (lode/ui/config-screen.md). */
#include <genesis.h>

#define RANGE_COUNT 5
extern const u16 range_steps[RANGE_COUNT];      /* 5, 10, 25, 50, 100 nm */

#define RELAY_LEN   63

enum tag_mode { TAGS_ALL, TAGS_SELECTED, TAGS_OFF, TAGS_MODES };

struct settings {
    u8 theme;                   /* THEME_CLASSIC / THEME_STARS */
    u8 range_idx;               /* index into range_steps */
    u8 tag_mode;
    char icao[5];               /* home airport */
    s32 lat_e4, lon_e4;         /* home point, 1e-4 deg */
    char relay[RELAY_LEN + 1];  /* relay base URL, "" = none */
};

extern struct settings settings;

/* Defaults from devcfg (range snapped to the nearest step). */
void settings_init(void);
/* Saved copy from ESP32 flash, if valid. Needs MegaWiFi up. TRUE if loaded. */
bool settings_load(void);
/* Write to ESP32 flash. TRUE on success. */
bool settings_save(void);
static inline u16 settings_range_nm(void) { return range_steps[settings.range_idx]; }

#endif
