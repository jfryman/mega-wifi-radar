/* src/feed.h on the 32X (lode/mars/link.md): every call is an RPC to the 68000, which runs the Genesis feed
 * (src/feed_relay.c or src/feed_fake.c). feed_rec() and feed_err_str() come from src/feed.c. */
#include "feed.h"
#include "settings.h"
#include "link_sh.h"
#include "../link.h"

static struct settings pushed;
static bool pushed_once;

/* src/feed_relay.c reads the settings (relay URL, home, range): send them when they've changed */
static void push_settings(void)
{
    if (pushed_once && !memcmp(&pushed, &settings, sizeof(settings))) return;
    pushed = settings;
    pushed_once = TRUE;
    lk_call(LK_SETTINGS, &pushed, sizeof(pushed), NULL, 0, NULL);
}

static union {
    struct { struct feed_stats st; u8 frame[AR_FRAME_MAX]; } f;
    u32 align;
} fb;

enum feed_err feed_fetch(const struct ar_hdr **hdr, struct feed_stats *st)
{
    push_settings();
    u8 e = lk_call(LK_FEED_FETCH, NULL, 0, &fb.f, sizeof(fb.f), NULL);
    *st = fb.f.st;
    if (e == FEED_OK) *hdr = (const struct ar_hdr *)fb.f.frame;
    return e;
}

enum feed_err feed_detail(const u8 icao[3], const struct ar_detail **d)
{
    static struct ar_detail det;
    u8 a[4] = { icao[0], icao[1], icao[2], 0 };
    push_settings();
    u8 e = lk_call(LK_FEED_DETAIL, a, 4, &det, sizeof(det), NULL);
    if (e == FEED_OK) *d = &det;
    return e;
}

/* The reply is /v1/terrain's body: the caller's hdr, h and c must be contiguous, in that order. */
enum feed_err feed_terrain(const u8 icao[3], struct ar_terrain *hdr, u8 *h, u8 *c)
{
    u8 a[4] = { icao[0], icao[1], icao[2], 0 };
    if (h != (u8 *)(hdr + 1) || c != h + AR_TN * AR_TN) return FEED_ERR_SIZE;
    push_settings();
    return lk_call(LK_FEED_TERRAIN, a, 4, hdr, AR_TERRAIN_SIZE, NULL);
}

enum feed_err feed_track(const u8 icao[3], const char *key, struct ar_track *t)
{
    u8 a[4 + AR_KEY_LEN];
    a[0] = icao[0]; a[1] = icao[1]; a[2] = icao[2]; a[3] = 0;
    strncpy((char *)a + 4, key, AR_KEY_LEN);
    a[4 + AR_KEY_LEN - 1] = 0;
    push_settings();
    return lk_call(LK_FEED_TRACK, a, sizeof(a), t, sizeof(*t), NULL);
}

enum feed_err feed_strip(const char *key, s16 x, s16 y, u16 w, u16 h, u8 *buf)
{
    u8 a[8 + AR_KEY_LEN];
    a[0] = (u16)x >> 8; a[1] = x; a[2] = (u16)y >> 8; a[3] = y;
    a[4] = w >> 8; a[5] = w; a[6] = h >> 8; a[7] = h;
    strncpy((char *)a + 8, key, AR_KEY_LEN);
    a[8 + AR_KEY_LEN - 1] = 0;
    push_settings();
    return lk_call(LK_FEED_STRIP, a, sizeof(a), buf, 2 * w * h, NULL);
}

enum feed_err feed_find_relay(void)
{
    push_settings();
    return lk_call(LK_FIND_RELAY, NULL, 0, NULL, 0, NULL);
}

bool feed_reconnect_due(void) { return lk_call(LK_RECONNECT_DUE, NULL, 0, NULL, 0, NULL); }

enum feed_err feed_reconnect(void)
{
    push_settings();
    return lk_call(LK_RECONNECT, NULL, 0, NULL, 0, NULL);
}

static char urls[2 * (RELAY_LEN + 2)];

static void get_urls(void)
{
    u16 n = 0;
    memset(urls, 0, sizeof(urls));
    lk_call(LK_RELAY_URL, NULL, 0, urls, sizeof(urls) - 2, &n);
}

const char *feed_relay_url(void) { get_urls(); return urls; }
const char *feed_relay_host(void) { get_urls(); return urls + strlen(urls) + 1; }
