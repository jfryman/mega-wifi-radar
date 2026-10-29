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
    char relay[RELAY_LEN + 1];  /* relay base URL, "" = AUTO (found over mDNS) */
};

extern struct settings settings;

/* Defaults: home point and range from devcfg (range snapped to the nearest step). */
void settings_init(void);
/* Dev-build defaults on top (DEV_RELAY_URL). Boot applies them unless settings_dev_suppressed(). */
void settings_dev_defaults(void);
/* TRUE after a setup reset on this same ROM build: boot then skips the dev Wi-Fi seed and
 * DEV_RELAY_URL, so first-boot flows can be tested on a dev build. Needs MegaWiFi up. */
bool settings_dev_suppressed(void);

/* Setup's RESET (lode/ui/config-screen.md). Also writes the dev-defaults-off marker. */
enum settings_reset { RESET_WIFI, RESET_SETTINGS, RESET_ALL };
bool settings_reset(enum settings_reset what);
/* Saved copy from ESP32 flash, if valid. Needs MegaWiFi up. TRUE if loaded. */
bool settings_load(void);
/* Write to ESP32 flash. TRUE on success. */
bool settings_save(void);
static inline u16 settings_range_nm(void) { return range_steps[settings.range_idx]; }

#endif
