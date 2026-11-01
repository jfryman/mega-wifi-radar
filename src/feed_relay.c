/* Feed from the relay: GET <relay>/v1/frame?lat&lon&range&max&v and /v1/detail?icao&v over MegaWiFi.
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
static u16 detail_buf[64];                      /* AR_DETAIL_SIZE + room to spot an oversized body */

struct collect {
    u16 len;
    bool overflow;
};
struct collect_to {
    struct collect c;
    u8 *buf;
    u16 size;
};

static void collect_chunk(const u8 *data, u16 len, void *ctx)
{
    struct collect_to *t = ctx;

    if (t->c.len + len > t->size) {
        t->c.overflow = TRUE;
        return;
    }
    memcpy(t->buf + t->c.len, data, len);
    t->c.len += len;
}

static char relay_url[RELAY_LEN + 1];   /* base URL in use: typed, or found over mDNS */

/* Reconnect policy (lode/net/reconnect.md) */
#define BACKOFF_MIN     15              /* seconds between reconnect attempts, doubling */
#define BACKOFF_MAX     60
static u16 failures;                    /* failed requests in a row */
static u16 backoff = BACKOFF_MIN;
static u32 next_at;                     /* vtimer: no reconnect before this */

enum feed_err feed_find_relay(void)
{
    if (settings.relay[0]) {
        strcpy(relay_url, settings.relay);
        return FEED_OK;
    }
    enum net_err e = net_discover(relay_url);
    if (e == NET_OK) return FEED_OK;
    relay_url[0] = 0;
    return e == NET_ERR_VERSION ? FEED_ERR_VERSION : FEED_ERR_CONFIG;
}

const char *feed_relay_url(void) { return relay_url; }

const char *feed_relay_host(void)
{
    return memcmp(relay_url, "http://", 7) ? relay_url : relay_url + 7;   /* buffer is RELAY_LEN + 1 */
}

bool feed_reconnect_due(void)
{
    return failures >= RELAY_RETRIES && (s32)(vtimer - next_at) >= 0;
}

enum feed_err feed_reconnect(void)
{
    enum feed_err e;
    u8 ip[4];

    next_at = vtimer + backoff * 60UL;
    if ((backoff *= 2) > BACKOFF_MAX) backoff = BACKOFF_MAX;
    relay_url[0] = 0;
    if (net_restart(ip) != NET_OK) e = FEED_ERR_WIFI;
    else e = feed_find_relay();
    /* failed: due again once next_at passes; ok: the next request decides */
    failures = e == FEED_OK ? 0 : RELAY_RETRIES;
    return e;
}

static bool build_url(char *url, u16 size)
{
    char lat[12], lon[12];

    if (!relay_url[0]) {
        enum feed_err e = feed_find_relay();
        if (e == FEED_ERR_CONFIG) failures++;           /* a desynced link can't run mDNS either */
        if (e != FEED_OK) return FALSE;
    }
    fmt_deg_e4(lat, settings.lat_e4);
    fmt_deg_e4(lon, settings.lon_e4);
    /* base (<= RELAY_LEN) + "/v1/frame?lat=-90.0000&lon=-180.0000&range=100&max=96&v=2" (<= 60) */
    sprintf(url, "%s" AR_FRAME_PATH AR_FRAME_QUERY, relay_url,
            lat, lon, settings_range_nm(), AR_MAX_RECS, AR_VER);
    return TRUE;
}

static enum feed_err check(const struct ar_hdr *h, u16 len)
{
    if (len < AR_HDR_SIZE || h->magic[0] != 'A' || h->magic[1] != 'R') return FEED_ERR_FORMAT;
    if (h->ver != AR_VER) return FEED_ERR_VERSION;      /* an older relay that ignores v= */
    if (h->rec_size < AR_REC_SIZE_V1 || (h->rec_size & 1) ||
            AR_HDR_SIZE + (u32)h->count * h->rec_size != len)
        return FEED_ERR_FORMAT;
    return FEED_OK;
}

/* GET url into buf. Counts failures for the reconnect; AR_HTTP_MISMATCH -> FEED_ERR_VERSION. */
static enum feed_err get(const char *url, struct collect_to *t, struct feed_stats *st)
{
    struct net_http_result res;
    enum net_err e = net_http_get(url, collect_chunk, t, &res);

    if (st) {
        st->http_status = res.status;
        st->bytes = t->c.len;
        st->chunks = res.chunks;
        st->frames = res.frames;
    }
    if (res.status == AR_HTTP_MISMATCH) return FEED_ERR_VERSION;
    if (e != NET_OK || res.status != 200) {
        failures++;
        return e == NET_ERR_RECV ? FEED_ERR_RECV : FEED_ERR_HTTP;
    }
    if (t->c.overflow) {
        failures++;
        return FEED_ERR_SIZE;
    }
    return FEED_OK;
}

/* A body that arrived but makes no sense is a link symptom too; a good one ends the failure run. */
static enum feed_err counted(enum feed_err e)
{
    if (e == FEED_OK) {
        failures = 0;
        backoff = BACKOFF_MIN;
    } else if (e != FEED_ERR_VERSION) {
        failures++;
    }
    return e;
}

enum feed_err feed_fetch(const struct ar_hdr **hdr, struct feed_stats *st)
{
    static char url[RELAY_LEN + 64];
    struct collect_to t = { { 0, FALSE }, (u8 *)frame_buf, sizeof(frame_buf) };
    enum feed_err e;

    memset(st, 0, sizeof(*st));
    if (!build_url(url, sizeof(url))) return FEED_ERR_CONFIG;
    if ((e = get(url, &t, st)) != FEED_OK) return e;
    if ((e = counted(check((const struct ar_hdr *)frame_buf, t.c.len))) != FEED_OK) return e;
    *hdr = (const struct ar_hdr *)frame_buf;
    return FEED_OK;
}

enum feed_err feed_detail(const u8 icao[3], const struct ar_detail **d)
{
    static char url[RELAY_LEN + 40];
    struct collect_to t = { { 0, FALSE }, (u8 *)detail_buf, sizeof(detail_buf) };
    const struct ar_detail *r = (const struct ar_detail *)detail_buf;
    enum feed_err e;

    if (!relay_url[0]) return FEED_ERR_CONFIG;
    /* base (<= RELAY_LEN) + "/v1/detail?icao=abfcda&v=2" (<= 28) */
    sprintf(url, "%s" AR_DETAIL_PATH AR_DETAIL_QUERY, relay_url, icao[0], icao[1], icao[2], AR_VER);
    if ((e = get(url, &t, NULL)) != FEED_OK) return e;
    if (t.c.len != AR_DETAIL_SIZE || r->magic[0] != 'A' || r->magic[1] != 'D') return counted(FEED_ERR_FORMAT);
    if (r->ver != AR_VER) return counted(FEED_ERR_VERSION);
    counted(FEED_OK);
    *d = r;
    return FEED_OK;
}

#endif
