#ifndef SPLASH_H
#define SPLASH_H

/* Boot splash: dawn airport, our wordmark with a shine, a 787 that taxis, holds and departs
 * as boot progresses (lode/ui/boot-splash.md). Animated entirely in the V-Int, so main can
 * block in mw_* calls. While it runs, main must not touch the VDP. */
#include <genesis.h>

enum splash_phase {
    SP_GATE,        /* detecting the module: "radio check" */
    SP_TAXI,        /* module found: taxi to the hold line */
    SP_HOLD,        /* joining Wi-Fi: hold short */
    SP_HOLD_POS,    /* join failed / no relay: hold position, detail says why */
    SP_TAKEOFF,     /* joined: cleared for takeoff, rolls and climbs away */
    SP_NO_MODULE,   /* detect failed: red detail, boot stops here */
};

/* Set up the scene (plane at the gate, or at the hold line for SP_HOLD) and start animating. */
void splash_start(enum splash_phase at);
/* Move to the next phase once the current call has been on screen long enough (START skips). */
void splash_phase(enum splash_phase p, const char *detail);
/* Dim status line under the radio box (firmware, IP). */
void splash_status(const char *s);
/* After SP_TAKEOFF: wait for the departure and "radar contact", fade out, stop. */
void splash_finish(void);
/* Stop now (e.g. before the setup screen) and blank the screen. */
void splash_stop(void);

#endif
