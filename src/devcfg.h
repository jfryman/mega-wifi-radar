#ifndef DEVCFG_H
#define DEVCFG_H

#include <genesis.h>

/* Dev-build settings from src/dev_config.h (see src/dev_config.h.example). */
extern const char *const devcfg_wifi_ssid;   /* NULL: use the module's saved AP */
extern const char *const devcfg_wifi_pass;
extern const char *const devcfg_relay_url;   /* NULL: not configured */

/* Home point (the scope centre). Dev builds default to KBNA; the start-up
 * config screen will replace these with user settings saved in ESP32 flash.
 * Degrees are fixed point, 1e-4 deg (~11 m): 36.1245 -> 361245. */
extern const char *const devcfg_home_icao;
extern const s32 devcfg_home_lat_e4;
extern const s32 devcfg_home_lon_e4;
extern const u16 devcfg_range_nm;

#endif
