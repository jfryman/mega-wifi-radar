#ifndef SETTINGS_H
#define SETTINGS_H

/* User settings: defaults from devcfg, edited on the setup screen, saved in
 * ESP32 user flash (lode/ui/config-screen.md). */
#include <genesis.h>

#define RANGE_COUNT 5
extern const u16 range_steps[RANGE_COUNT];      /* 5, 10, 25, 50, 100 nm */

#define RELAY_LEN   63

enum tag_mode { TAGS_ALL, TAGS_SELECTED, TAGS_OFF, TAGS_MODES };
enum sound_mode { SOUND_ON, SOUND_OFF, SOUND_MODES };     /* boot sound + the flyover chime */
enum ambient_mode { AMBIENT_OFF, AMBIENT_1, AMBIENT_5, AMBIENT_15, AMBIENT_MODES };   /* idle minutes (ambient.c) */
enum wx_mode { WX_ON, WX_OFF, WX_MODES };      /* the weather overlay (wx.c) */
enum map_mode { MAP_ON, MAP_OFF, MAP_MODES };  /* video maps (vmap.c) */

struct settings {
    u8 theme;                   /* enum THEME_* (theme.h) */
    u8 range_idx;               /* index into range_steps */
    u8 tag_mode;
    char icao[5];               /* home airport */
    s32 lat_e4, lon_e4;         /* home point, 1e-4 deg */
    char relay[RELAY_LEN + 1];  /* relay base URL, "" = AUTO (found over mDNS) */
    u8 sound;                   /* SOUND_ON / SOUND_OFF (was CHIME: same byte, same values) */
    u8 sort;                    /* traffic list order (enum sort_mode, tlist.h); MWR3 on */
    u8 ambient;                 /* enum ambient_mode (ambient.h); the first spare byte, 0 = OFF */
    u8 wx;                      /* enum wx_mode; the second spare byte, 0 = ON */
    u8 map;                     /* enum map_mode; the third spare byte, 0 = ON */
    u8 rsv[4];                  /* spare, 0 until a later build gives one a meaning (and 0 its default) */
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
/* A setting changed on the radar (zoom, tags, theme, sort): settings_save_due() says when to write it, from
 * the radar's poll (a blocking flash write, like the poll's own HTTP), at most every SAVE_GAP_SECS. */
void settings_changed(void);
bool settings_save_due(void);
static inline u16 settings_range_nm(void) { return range_steps[settings.range_idx]; }

#endif
