#ifndef NOTICE_H
#define NOTICE_H

/* Flyover notices (lode/ui/notices.md): a plane over the owner's house, from the relay's MQTT feed. The relay
 * composes three lines; the radar shows them in a banner over the top of the scope, cockpit mode on its HUD.
 * Main loop only. */
#include <genesis.h>
#include "arframe.h"

/* After a frame or track: fetch a waiting notice (one small blocking GET) and play the chime. */
void notice_check(void);

/* ---- radar ---- */
/* Every frame: show a pending notice in the banner (once the detail pop-up is closed), blink, time it out. */
void notice_radar_tick(void);
bool notice_banner_up(void);
void notice_banner_close(void);         /* B, or the radar is about to stop */
/* C with the banner up: select its aircraft (if the relay matched one) so the detail pop-up opens on it. */
void notice_select(void);

/* ---- cockpit mode ---- */
/* The notice to show (NULL if none), and done with it. */
const struct ar_notice *notice_current(void);
void notice_done(void);

/* Setup opened: nobody would see a pending notice in time. */
void notice_drop(void);

#endif
