/* Feed from the relay: GET <relay>/v1/frame?lat&lon&range&max over MegaWiFi.
 * Contract: lode/relay/relay.md, format: lode/architecture/frame-format.md. */
#include "feed.h"

#ifndef FAKE_FEED
#include "devcfg.h"
#include "fmt.h"
#include "net.h"
#include "settings.h"

/* Word-aligned: the 68000 faults on odd-address word/long reads, and records
 * are read in place through struct pointers. */
static u16 frame_buf[AR_FRAME_MAX / 2];

struct collect {
    u16 len;
    bool overflow;
};

static void collect_chunk(const u8 *data, u16 len, void *ctx)
{
    struct collect *c = ctx;

    if (c->len + len > sizeof(frame_buf)) {
        c->overflow = TRUE;
        return;
    }
    memcpy((u8 *)frame_buf + c->len, data, len);
    c->len += len;
}

static bool build_url(char *url, u16 size)
{
    char lat[12], lon[12];

    if (!settings.relay[0]) return FALSE;
    fmt_deg_e4(lat, settings.lat_e4);
    fmt_deg_e4(lon, settings.lon_e4);
    /* base (<= RELAY_LEN) + "/v1/frame?lat=-90.0000&lon=-180.0000&range=100&max=96" (<= 56) */
    sprintf(url, "%s/v1/frame?lat=%s&lon=%s&range=%u&max=%u", settings.relay,
            lat, lon, settings_range_nm(), AR_MAX_RECS);
    return TRUE;
}

static bool valid(const struct ar_hdr *h, u16 len)
{
    return len >= AR_HDR_SIZE && h->magic[0] == 'A' && h->magic[1] == 'R' &&
           h->ver == AR_VER && h->rec_size >= AR_REC_SIZE_V1 && !(h->rec_size & 1) &&
           AR_HDR_SIZE + (u32)h->count * h->rec_size == len;
}

enum feed_err feed_fetch(const struct ar_hdr **hdr, struct feed_stats *st)
{
    static char url[RELAY_LEN + 64];
    struct net_http_result res;
    struct collect c = { 0, FALSE };
    enum net_err e;

    memset(st, 0, sizeof(*st));
    if (!build_url(url, sizeof(url))) return FEED_ERR_CONFIG;

    e = net_http_get(url, collect_chunk, &c, &res);
    st->http_status = res.status;
    st->bytes = c.len;
    st->chunks = res.chunks;
    st->frames = res.frames;
    if (e == NET_ERR_RECV) return FEED_ERR_RECV;
    if (e != NET_OK || res.status != 200) return FEED_ERR_HTTP;
    if (c.overflow) return FEED_ERR_SIZE;
    if (!valid((const struct ar_hdr *)frame_buf, c.len)) return FEED_ERR_FORMAT;
    *hdr = (const struct ar_hdr *)frame_buf;
    return FEED_OK;
}

#endif
