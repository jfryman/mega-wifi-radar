#ifndef CHIME_H
#define CHIME_H

/* The flyover notice chime (lode/ui/notices.md): an FM bell on the YM2612, or for home airport KBNA a short
 * steel-guitar lick; and the emergency alert's tone. settings.sound turns them off. */
#include <genesis.h>

/* Start it (cuts off one that's playing). Main loop. */
void chime_play(void);
/* The emergency-squawk alert instead (alert.c): a hi-lo tone, three times. */
void chime_alert(void);
/* Key notes on and off as their time comes: every frame, from the radar's V-Int, or from the cockpit and
 * setup loops (it goes by vtimer, so a slower caller only delays notes a little). */
void chime_tick(void);

#endif
