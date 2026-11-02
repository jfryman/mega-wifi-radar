/* Feed from the relay: GET <relay>/v1/frame?lat&lon&range&max&n&w&v, /v1/detail?icao&v, /v1/notice?after&v,
 * /v1/wx?lat&lon&range&v, /v1/map?lat&lon&range&v, /v1/log?lat&lon&v, and
 * cockpit mode's /v1/terrain?icao&v, /v1/track?icao&p&n&v and /v1/strip?p&x&y&w&h&v over MegaWiFi.
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

/* Flyover notices: the last id seen (sent as n=; 0 = none yet) and whether the relay says a newer one waits. */
static u16 notice_seen;
static bool notice_flag;

/* Weather: the id of the last one fetched (sent as w=; 0 = none) and whether the relay has another. */
static u16 wx_held;
static bool wx_flag;

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
    /* base (<= RELAY_LEN) + "/v1/frame?lat=-90.0000&lon=-180.0000&range=100&max=96&n=65535&w=65535&v=5" (<= 76) */
    sprintf(url, "%s" AR_FRAME_PATH AR_FRAME_QUERY, relay_url,
            lat, lon, settings_range_nm(), AR_MAX_RECS, notice_seen, wx_held, AR_VER);
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

/* GET url through cb (its ctx starts with a struct collect). Counts failures for the reconnect;
 * AR_HTTP_MISMATCH -> FEED_ERR_VERSION. */
static enum feed_err get_cb(const char *url, net_chunk_cb cb, struct collect *c, struct feed_stats *st)
{
    struct net_http_result res;
    enum net_err e = net_http_get(url, cb, c, &res);

    if (st) {
        st->http_status = res.status;
        st->bytes = c->len;
        st->chunks = res.chunks;
        st->frames = res.frames;
    }
    if (res.status == AR_HTTP_MISMATCH) return FEED_ERR_VERSION;
    if (e != NET_OK || res.status != 200) {
        failures++;
        return e == NET_ERR_RECV ? FEED_ERR_RECV : FEED_ERR_HTTP;
    }
    if (c->overflow) {
        failures++;
        return FEED_ERR_SIZE;
    }
    return FEED_OK;
}

static enum feed_err get(const char *url, struct collect_to *t, struct feed_stats *st)
{
    return get_cb(url, collect_chunk, &t->c, st);
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
    static char url[RELAY_LEN + 80];
    struct collect_to t = { { 0, FALSE }, (u8 *)frame_buf, sizeof(frame_buf) };
    enum feed_err e;

    memset(st, 0, sizeof(*st));
    if (!build_url(url, sizeof(url))) return FEED_ERR_CONFIG;
    if ((e = get(url, &t, st)) != FEED_OK) return e;
    if ((e = counted(check((const struct ar_hdr *)frame_buf, t.c.len))) != FEED_OK) return e;
    *hdr = (const struct ar_hdr *)frame_buf;
    notice_flag = (*hdr)->hflags & AR_H_NOTICE;
    wx_flag = (*hdr)->hflags & AR_H_WX;
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

/* /v1/terrain: the header, then the heights into h and the colours into c, as the bytes arrive. */
struct terrain_sink {
    struct collect c;               /* only .overflow: the body is bigger than c.len can count */
    u32 total;                      /* bytes so far */
    struct ar_terrain *hdr;
    u8 *h, *col;
};

static void terrain_chunk(const u8 *data, u16 len, void *ctx)
{
    struct terrain_sink *t = ctx;

    while (len) {
        u32 at = t->total;
        u8 *dst;
        u32 room;
        if (at < AR_TERRAIN_HDR) {
            dst = (u8 *)t->hdr + at;
            room = AR_TERRAIN_HDR - at;
        } else if (at < AR_TERRAIN_HDR + AR_TN * AR_TN) {
            dst = t->h + (at - AR_TERRAIN_HDR);
            room = AR_TERRAIN_HDR + AR_TN * AR_TN - at;
        } else if (at < AR_TERRAIN_SIZE) {
            dst = t->col + (at - AR_TERRAIN_HDR - AR_TN * AR_TN);
            room = AR_TERRAIN_SIZE - at;
        } else {
            t->c.overflow = TRUE;
            return;
        }
        u16 n = len < room ? len : room;
        memcpy(dst, data, n);
        data += n;
        len -= n;
        t->total += n;
    }
}

enum feed_err feed_terrain(const u8 icao[3], struct ar_terrain *hdr, u8 *h, u8 *c)
{
    static char url[RELAY_LEN + 40];
    struct terrain_sink t = { { 0, FALSE }, 0, hdr, h, c };
    enum feed_err e;

    if (!relay_url[0]) return FEED_ERR_CONFIG;
    /* base (<= RELAY_LEN) + "/v1/terrain?icao=abfcda&v=3" (<= 29) */
    sprintf(url, "%s" AR_TERRAIN_PATH AR_TERRAIN_QUERY, relay_url, icao[0], icao[1], icao[2], AR_VER);
    if ((e = get_cb(url, terrain_chunk, &t.c, NULL)) != FEED_OK) return e;
    if (t.total != AR_TERRAIN_SIZE || hdr->magic[0] != 'A' || hdr->magic[1] != 'T' || hdr->n != AR_TN)
        return counted(FEED_ERR_FORMAT);
    if (hdr->ver != AR_VER) return counted(FEED_ERR_VERSION);
    hdr->key[AR_KEY_LEN - 1] = 0;
    return counted(FEED_OK);
}

enum feed_err feed_track(const u8 icao[3], const char *key, struct ar_track *out)
{
    static char url[RELAY_LEN + 40 + AR_KEY_LEN];
    static u16 buf[AR_TRACK_SIZE / 2 + 4];          /* + room to spot an oversized body */
    struct collect_to t = { { 0, FALSE }, (u8 *)buf, sizeof(buf) };
    const struct ar_track *r = (const struct ar_track *)buf;
    enum feed_err e;

    if (!relay_url[0]) return FEED_ERR_CONFIG;
    /* base (<= RELAY_LEN) + "/v1/track?icao=abfcda&p=<key>&n=65535&v=4" (<= 35 + key) */
    sprintf(url, "%s" AR_TRACK_PATH AR_TRACK_QUERY, relay_url, icao[0], icao[1], icao[2], key, notice_seen, AR_VER);
    if ((e = get(url, &t, NULL)) != FEED_OK) return e;
    if (t.c.len != AR_TRACK_SIZE || r->magic[0] != 'A' || r->magic[1] != 'K') return counted(FEED_ERR_FORMAT);
    if (r->ver != AR_VER) return counted(FEED_ERR_VERSION);
    *out = *r;
    notice_flag = r->kflags & AR_K_NOTICE;
    return counted(FEED_OK);
}

bool feed_notice_waiting(void) { return notice_flag; }

enum feed_err feed_notice(const struct ar_notice **n)
{
    static char url[RELAY_LEN + 32];
    static u16 buf[AR_NOTICE_SIZE / 2 + 4];         /* + room to spot an oversized body */
    struct collect_to t = { { 0, FALSE }, (u8 *)buf, sizeof(buf) };
    const struct ar_notice *r = (const struct ar_notice *)buf;
    enum feed_err e;

    notice_flag = FALSE;                            /* the next frame or track says if another waits */
    if (!relay_url[0]) return FEED_ERR_CONFIG;
    /* base (<= RELAY_LEN) + "/v1/notice?after=65535&v=4" (<= 26) */
    sprintf(url, "%s" AR_NOTICE_PATH AR_NOTICE_QUERY, relay_url, notice_seen, AR_VER);
    if ((e = get(url, &t, NULL)) != FEED_OK) return e;
    if (t.c.len != AR_NOTICE_SIZE || r->magic[0] != 'A' || r->magic[1] != 'N') return counted(FEED_ERR_FORMAT);
    if (r->ver != AR_VER) return counted(FEED_ERR_VERSION);
    if (r->id) notice_seen = r->id;
    *n = r;
    return counted(FEED_OK);
}

bool feed_wx_waiting(void) { return wx_flag; }

enum feed_err feed_wx(const struct ar_wx **w)
{
    static char url[RELAY_LEN + 64];
    static u16 buf[AR_WX_SIZE / 2 + 4];             /* + room to spot an oversized body */
    struct collect_to t = { { 0, FALSE }, (u8 *)buf, sizeof(buf) };
    const struct ar_wx *r = (const struct ar_wx *)buf;
    char lat[12], lon[12];
    enum feed_err e;

    if (!relay_url[0]) return FEED_ERR_CONFIG;
    fmt_deg_e4(lat, settings.lat_e4);
    fmt_deg_e4(lon, settings.lon_e4);
    /* base (<= RELAY_LEN) + "/v1/wx?lat=-90.0000&lon=-180.0000&range=100&v=5" (<= 48) */
    sprintf(url, "%s" AR_WX_PATH AR_WX_QUERY, relay_url, lat, lon, settings_range_nm(), AR_VER);
    if ((e = get(url, &t, NULL)) != FEED_OK) return e;
    if (t.c.len != AR_WX_SIZE || r->magic[0] != 'A' || r->magic[1] != 'W') return counted(FEED_ERR_FORMAT);
    if (r->ver != AR_VER) return counted(FEED_ERR_VERSION);
    wx_held = r->id;
    wx_flag = FALSE;
    *w = r;
    return counted(FEED_OK);
}

enum feed_err feed_map(u8 *buf, u16 size)
{
    static char url[RELAY_LEN + 64];
    struct collect_to t = { { 0, FALSE }, buf, size };
    const struct ar_map *m = (const struct ar_map *)buf;
    char lat[12], lon[12];
    enum feed_err e;

    if (!relay_url[0]) return FEED_ERR_CONFIG;
    fmt_deg_e4(lat, settings.lat_e4);
    fmt_deg_e4(lon, settings.lon_e4);
    /* base (<= RELAY_LEN) + "/v1/map?lat=-90.0000&lon=-180.0000&range=100&v=6" (<= 49) */
    sprintf(url, "%s" AR_MAP_PATH AR_MAP_QUERY, relay_url, lat, lon, settings_range_nm(), AR_VER);
    if ((e = get(url, &t, NULL)) != FEED_OK) return e;
    if (t.c.len < AR_MAP_HDR || m->magic[0] != 'A' || m->magic[1] != 'M') return counted(FEED_ERR_FORMAT);
    if (m->ver != AR_VER) return counted(FEED_ERR_VERSION);
    u16 n = m->runways + m->centres + m->ticks;
    if (n > AR_MAP_MAX || t.c.len != AR_MAP_HDR + n * sizeof(struct ar_seg)) return counted(FEED_ERR_FORMAT);
    return counted(FEED_OK);
}

enum feed_err feed_log(const struct ar_log **l)
{
    static char url[RELAY_LEN + 48];
    struct collect_to t = { { 0, FALSE }, (u8 *)frame_buf, sizeof(frame_buf) };    /* the frame is ingested by now */
    const struct ar_log *r = (const struct ar_log *)frame_buf;
    char lat[12], lon[12];
    enum feed_err e;

    if (!relay_url[0]) return FEED_ERR_CONFIG;
    fmt_deg_e4(lat, settings.lat_e4);
    fmt_deg_e4(lon, settings.lon_e4);
    /* base (<= RELAY_LEN) + "/v1/log?lat=-90.0000&lon=-180.0000&v=7" (<= 39) */
    sprintf(url, "%s" AR_LOG_PATH AR_LOG_QUERY, relay_url, lat, lon, AR_VER);
    if ((e = get(url, &t, NULL)) != FEED_OK) return e;
    if (t.c.len != AR_LOG_SIZE || r->magic[0] != 'A' || r->magic[1] != 'L') return counted(FEED_ERR_FORMAT);
    if (r->ver != AR_VER) return counted(FEED_ERR_VERSION);
    *l = r;
    return counted(FEED_OK);
}

enum feed_err feed_strip(const char *key, s16 x, s16 y, u16 w, u16 h, u8 *buf)
{
    static char url[RELAY_LEN + 56 + AR_KEY_LEN];
    struct collect_to t = { { 0, FALSE }, buf, 2 * w * h };
    enum feed_err e;

    if (!relay_url[0]) return FEED_ERR_CONFIG;
    /* base (<= RELAY_LEN) + "/v1/strip?p=<key>&x=-30000&y=-30000&w=128&h=128&v=3" (<= 53 + key) */
    sprintf(url, "%s" AR_STRIP_PATH AR_STRIP_QUERY, relay_url, key, (int)x, (int)y, w, h, AR_VER);
    if ((e = get(url, &t, NULL)) != FEED_OK) return e;
    return counted(t.c.len == 2 * w * h ? FEED_OK : FEED_ERR_FORMAT);
}

#endif
