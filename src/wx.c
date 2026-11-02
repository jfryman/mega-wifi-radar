/* Weather overlay (lode/ui/weather.md). The relay samples NEXRAD on the scope's cell grid at the cart's range;
 * this fetches it when the frame says it's new (AR_H_WX), when the range on screen isn't the range fetched (a
 * zoom wipes the cells), and when the radar starts. settings.wx = WX_OFF clears it and stops fetching. */
#include "wx.h"
#include "feed.h"
#include "radar.h"
#include "settings.h"

static u16 range;           /* the range of the cells on screen, 0 = none */
static bool source;         /* they come from a weather source (not AR_W_NONE): credit it */
static u32 retry_at;        /* vtimer: after a failed fetch, not before this */

#define RETRY_SECS  60

void wx_reset(void)
{
    range = 0;
    source = FALSE;
    retry_at = vtimer;
}

void wx_check(void)
{
    const struct ar_wx *w;

    if (settings.wx == WX_OFF) {
        if (range) radar_wx(NULL);
        wx_reset();
        return;
    }
    if (range == settings_range_nm() && !feed_wx_waiting()) return;
    if ((s32)(vtimer - retry_at) < 0) return;
    if (feed_wx(&w) != FEED_OK) {           /* the relay can't reach its weather source: not every poll */
        retry_at = vtimer + RETRY_SECS * 60UL;
        return;
    }
    range = w->range_nm == settings_range_nm() ? w->range_nm : 0;
    source = w->id && !(w->wflags & AR_W_NONE);
    radar_wx(range && source ? w->cells : NULL);
}

bool wx_shown(void) { return range && source && settings.wx == WX_ON; }
