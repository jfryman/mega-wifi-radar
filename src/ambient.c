#include "ambient.h"
#include "fmt.h"
#include "notice.h"
#include "popup.h"
#include "radar.h"
#include "settings.h"
#include "tlist.h"
#include "traffic.h"

#define DETAILS_SECS    8       /* the pop-up, before the ride */
#define RADAR_SECS      60      /* on the radar between rides */
#define RETRY_SECS      10      /* nothing to pick: look again */
#define NEAR            (10 * 32)   /* 10 nm in frame units: low traffic here makes the best ride */

static const u8 minutes[AMBIENT_MODES] = { 0, 1, 5, 15 };

static enum { OFF, RADAR, DETAILS } state;
static u32 idle_since, at;
static u8 icao[3], last[3];

bool ambient_on(void) { return state != OFF; }
const u8 *ambient_icao(void) { return icao; }

static void stop(void)
{
    state = OFF;
    idle_since = vtimer;
}

bool ambient_input(u16 joy)
{
    if (!joy) return FALSE;
    idle_since = vtimer;
    if (state == OFF) return FALSE;
    if (popup_is_open()) popup_close();
    stop();
    return TRUE;
}

/* Score a candidate: emergencies first, then low traffic near home (the lower the better), then the nearest.
 * Higher is better; -1 = not a candidate. Ground, stale and unknown-altitude traffic make poor rides. */
static s32 score(const struct ar_rec *r)
{
    if (r->flags & (AR_F_GROUND | AR_F_STALE | AR_F_ALT_UNK)) return -1;
    u16 d = isqrt32((u32)((s32)r->x * r->x) + (u32)((s32)r->y * r->y));
    if (r->flags & AR_F_EMERG) return 0x30000L - d;
    if (d < NEAR) return 0x20000L - (r->alt < 0 ? 0 : r->alt);
    return 0x10000L - d;
}

static s16 pick(void)
{
    s16 best = -1, again = -1;
    s32 bs = -1, as = -1;

    for (u16 i = 0; i < TR_MAX; i++) {
        if (!tracks[i].used || tracks[i].drop) continue;
        s32 s = score(&tracks[i].next);
        if (s < 0) continue;
        if (!memcmp(tracks[i].next.icao, last, 3)) {    /* the last ride: only if there's nothing else */
            if (s > as) { as = s; again = i; }
        } else if (s > bs) {
            bs = s;
            best = i;
        }
    }
    return best >= 0 ? best : again;
}

enum ambient_act ambient_tick(void)
{
    if (settings.ambient == AMBIENT_OFF) {
        if (state != OFF) {
            stop();
            return AMB_PANEL;
        }
        return AMB_NONE;
    }
    switch (state) {
    case OFF:
        if (vtimer - idle_since < minutes[settings.ambient] * 3600UL) return AMB_NONE;
        state = RADAR;
        at = vtimer - RADAR_SECS * 60UL;    /* the first ride straight away */
        if (popup_is_open()) popup_close();
        return AMB_PANEL;
    case RADAR: {
        if (vtimer - at < RADAR_SECS * 60UL) return AMB_NONE;
        s16 i = pick();
        if (i < 0) {
            at = vtimer - (RADAR_SECS - RETRY_SECS) * 60UL;
            return AMB_NONE;
        }
        memcpy(icao, tracks[i].next.icao, 3);
        radar_select(i);
        tlist_rebuild();                    /* scroll the list to it */
        notice_banner_close(TRUE);
        popup_open();
        state = DETAILS;
        at = vtimer;
        return AMB_NONE;
    }
    case DETAILS:
        if (vtimer - at < DETAILS_SECS * 60UL) return AMB_NONE;
        memcpy(last, icao, 3);
        return AMB_COCKPIT;
    }
    return AMB_NONE;
}

void ambient_back(bool by_user)
{
    if (by_user) {
        stop();
        return;
    }
    state = RADAR;
    at = vtimer;
}
