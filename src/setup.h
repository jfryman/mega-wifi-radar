#ifndef SETUP_H
#define SETUP_H

/* Setup screen (lode/ui/config-screen.md): theme, range, tags, home airport,
 * Wi-Fi (scan + keyboard), relay URL; saved to ESP32 flash. Runs with the
 * radar renderer stopped. wifi_first opens the Wi-Fi page straight away
 * (boot without a working network). Returns TRUE if settings were saved. */
#include <genesis.h>

bool setup_run(bool wifi_first);

#endif
