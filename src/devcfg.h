#ifndef DEVCFG_H
#define DEVCFG_H

#include <genesis.h>

/* Dev-build settings from src/dev_config.h (see src/dev_config.h.example). */
extern const char *const devcfg_wifi_ssid;   /* NULL: use the module's saved AP */
extern const char *const devcfg_wifi_pass;
extern const char *const devcfg_http_url;

#endif
