/* The only file that includes the optional, git-ignored src/dev_config.h and
 * the generated src/version.h. make deletes this object before every build:
 * SGDK's dependency files can't see a header that didn't exist at the last
 * build, so without that, creating or editing either would silently leave the
 * old values in the ROM. */
#include "devcfg.h"

#if __has_include("version.h")
#include "version.h"
#endif
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif
const char *const app_version = APP_VERSION;

#if __has_include("dev_config.h")
#include "dev_config.h"
#endif

#ifdef DEV_WIFI_SSID
const char *const devcfg_wifi_ssid = DEV_WIFI_SSID;
const char *const devcfg_wifi_pass = DEV_WIFI_PASS;
#else
const char *const devcfg_wifi_ssid = NULL;
const char *const devcfg_wifi_pass = NULL;
#endif

/* Nashville International (OurAirports: 36.1245, -86.6782, elev 599 ft). */
#ifndef DEV_HOME_ICAO
#define DEV_HOME_ICAO    "KBNA"
#define DEV_HOME_LAT_E4  361245
#define DEV_HOME_LON_E4  (-866782)
#endif
#ifndef DEV_RANGE_NM
#define DEV_RANGE_NM     25
#endif
const char *const devcfg_home_icao = DEV_HOME_ICAO;
const s32 devcfg_home_lat_e4 = DEV_HOME_LAT_E4;
const s32 devcfg_home_lon_e4 = DEV_HOME_LON_E4;
const u16 devcfg_range_nm = DEV_RANGE_NM;

/* Base URL of the relay (relay/), e.g. "http://host.example.lan:8080". */
#ifdef DEV_RELAY_URL
const char *const devcfg_relay_url = DEV_RELAY_URL;
#else
const char *const devcfg_relay_url = NULL;
#endif
