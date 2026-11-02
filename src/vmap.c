/* Video maps (lode/ui/video-maps.md). The relay projects runways and the home airport's extended centrelines into
 * scope pixels at the cart's range; this fetches them into the renderer's buffer when the range on screen isn't the
 * range fetched (start, zoom: a zoom wipes the map). settings.map = MAP_OFF removes it and stops fetching. */
#include "vmap.h"
#include "feed.h"
#include "radar.h"
#include "settings.h"

#define RETRY_SECS  60

static u16 range;           /* the range of the map on screen, 0 = none */
static u32 retry_at;        /* vtimer: after a failed fetch, not before this */

void vmap_reset(void)
{
    range = 0;
    retry_at = vtimer;
}

void vmap_check(void)
{
    u16 size;

    if (settings.map == MAP_OFF) {
        if (range) {
            radar_map_begin(&size);
            radar_map_end(FALSE);
        }
        vmap_reset();
        return;
    }
    if (range == settings_range_nm() || (s32)(vtimer - retry_at) < 0) return;
    u8 *buf = radar_map_begin(&size);
    bool ok = feed_map(buf, size) == FEED_OK;
    radar_map_end(ok);
    if (ok) range = settings_range_nm();
    else retry_at = vtimer + RETRY_SECS * 60UL;     /* a relay without maps: not every poll */
}
