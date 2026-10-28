/* The only file that includes the optional, git-ignored src/dev_config.h.
 * make deletes this object before every build: SGDK's dependency files can't
 * see a header that didn't exist at the last build, so without that, creating
 * or editing dev_config.h would silently leave the old values in the ROM. */
#include "devcfg.h"

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

#ifdef DEV_HTTP_URL
const char *const devcfg_http_url = DEV_HTTP_URL;
#else
const char *const devcfg_http_url = "http://example.com/";
#endif
