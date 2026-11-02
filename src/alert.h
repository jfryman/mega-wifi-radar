#ifndef ALERT_H
#define ALERT_H

/* Emergency alert (lode/ui/notices.md): an aircraft squawking 7500/7600/7700 (or reporting an emergency) on two
 * polls in a row gets the notice box and a hi-lo tone, once per episode, at most one alert a minute.
 * Main loop only, after traffic_ingest. */
#include <genesis.h>

void alert_check(void);

#endif
