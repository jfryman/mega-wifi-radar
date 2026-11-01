/* Flyover notices (lode/ui/notices.md). One notice at a time: a newer one replaces it. In the radar it waits
 * (up to WAIT_SECS) while the detail pop-up is open, then shows in the box over the scope's centre for its secs;
 * the detail pop-up opening ends it. Cockpit mode takes it from notice_current(). */
#include "notice.h"
#include "chime.h"
#include "feed.h"
#include "popup.h"
#include "radar.h"
#include "theme.h"
#include "tlist.h"
#include "traffic.h"

#define WAIT_SECS   30      /* a notice not shown by then is dropped */
#define DEFAULT_SECS 15
#define BLINK       15      /* frames per blink of the headline ... */
#define BLINK_FOR   120     /* ... for this long */

static struct ar_notice cur;
static bool have;           /* cur is pending or showing */
static bool shown;          /* in the banner */
static u32 got_at, shown_at;

void notice_check(void)
{
    const struct ar_notice *n;

    if (!feed_notice_waiting()) return;
    if (feed_notice(&n) != FEED_OK || !n->id) return;
    cur = *n;
    if (!cur.secs) cur.secs = DEFAULT_SECS;     /* cockpit pages are secs / 3: never 0 */
    have = TRUE;
    shown = FALSE;          /* the banner (if up) is rewritten with this one */
    got_at = vtimer;
    chime_play();
}

/* ------------------------------------------------------------ radar box */

/* Select the notice's aircraft, if the relay matched one and it's in the track table. */
static void select_it(void)
{
    if (!(cur.nflags & AR_N_ICAO)) return;
    for (u16 i = 0; i < TR_MAX; i++)
        if (tracks[i].used && !memcmp(tracks[i].next.icao, cur.icao, 3)) {
            radar_select(i);
            tlist_rebuild();
            return;
        }
}

static u16 head_pal(void) { return (cur.nflags & AR_N_EMERG) ? PAL_TEXT_EMERG : PAL2; }

static void show(void)
{
    char text[4][AR_NOTICE_COLS + 1];
    u8 pal[4] = { head_pal(), PAL0, PAL0, PAL1 };

    select_it();                            /* its blip gets the box; C opens its detail */
    memcpy(text, cur.text, 3 * AR_NOTICE_COLS);     /* rows of AR_NOTICE_COLS: the 4th is the hint */
    char *t = (char *)text;
    memcpy(t + 3 * AR_NOTICE_COLS, (cur.nflags & AR_N_ICAO) ? "C DETAILS      B DISMISS" : "               B DISMISS",
           AR_NOTICE_COLS);
    radar_box_open(t, pal);
    shown = TRUE;
    shown_at = vtimer;
}

void notice_banner_close(bool now)
{
    if (!have || !shown) return;
    have = shown = FALSE;
    radar_box_close(now);
}

void notice_radar_tick(void)
{
    if (!have) return;
    if (popup_is_open()) {
        if (shown) notice_banner_close(TRUE);   /* under the pop-up: gone without a show */
        else if (vtimer - got_at > WAIT_SECS * 60UL) have = FALSE;
        return;
    }
    if (!shown) {
        if (vtimer - got_at > WAIT_SECS * 60UL) have = FALSE;
        else show();
        return;
    }
    u32 t = vtimer - shown_at;
    if (t >= cur.secs * 60UL) {
        notice_banner_close(FALSE);
        return;
    }
    if (t < BLINK_FOR) radar_box_pal(0, (t / BLINK) & 1 ? PAL0 : head_pal());
}

bool notice_banner_up(void) { return have && shown; }

void notice_select(void)
{
    if (notice_banner_up()) select_it();
}

/* ------------------------------------------------------------ cockpit, setup */

const struct ar_notice *notice_current(void)
{
    if (have && vtimer - got_at > (WAIT_SECS + cur.secs) * 60UL) have = FALSE;
    return have ? &cur : NULL;
}

void notice_done(void) { have = shown = FALSE; }
void notice_drop(void) { have = shown = FALSE; }
