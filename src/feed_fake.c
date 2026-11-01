/* Fake feed for emulators (build with FAKE_FEED): cycles through frames
 * recorded around KBNA (res/fake/kbna_seq.bin, made by `relay -pack`), with their aircraft's
 * pop-up details (res/fake/kbna_detail.bin) and, for cockpit mode, one terrain patch around KBNA with every
 * aircraft's track in it (res/fake/kbna_cockpit.bin, `relay -pack-cockpit`). */
#include "feed.h"

#ifdef FAKE_FEED
#include "resources.h"

/* "FK", u16 count, count x u32 offsets, then word-aligned v1 frames. */
enum feed_err feed_fetch(const struct ar_hdr **hdr, struct feed_stats *st)
{
    static u16 next;
    const u8 *pack = fake_seq_rom;
    u16 count = *(const u16 *)(pack + 2);
    u32 off = *(const u32 *)(pack + 4 + 4 * next);
    const struct ar_hdr *h = (const struct ar_hdr *)(pack + off);

    memset(st, 0, sizeof(*st));
    if (pack[0] != 'F' || pack[1] != 'K' || !count) return FEED_ERR_FORMAT;
    next = (next + 1) % count;
    st->http_status = 200;
    st->bytes = AR_HDR_SIZE + h->count * h->rec_size;
    st->chunks = 1;
    *hdr = h;
    return FEED_OK;
}
/* "FD", u16 count, then count x struct ar_detail (res/fake/kbna_detail.bin, `relay -pack-detail`). */
enum feed_err feed_detail(const u8 icao[3], const struct ar_detail **d)
{
    const u8 *pack = fake_detail_rom;
    u16 count = *(const u16 *)(pack + 2);
    const struct ar_detail *r = (const struct ar_detail *)(pack + 4);

    for (u16 i = 0; i < count; i++, r++)
        if (r->icao[0] == icao[0] && r->icao[1] == icao[1] && r->icao[2] == icao[2]) {
            *d = r;
            return FEED_OK;
        }
    return FEED_ERR_HTTP;
}
/* "FC", u16 frames, u16 0, the /v1/terrain body, then per frame: u16 count, u16 deciseconds since the first,
 * count x { u8 icao[3], u8 0, struct ar_track }. */
#define FC_TERRAIN  6
#define FC_ENTRY    (4 + AR_TRACK_SIZE)

enum feed_err feed_terrain(const u8 icao[3], struct ar_terrain *hdr, u8 *h, u8 *c)
{
    const u8 *pack = fake_cockpit_rom;

    (void)icao;
    if (pack[0] != 'F' || pack[1] != 'C') return FEED_ERR_FORMAT;
    memcpy(hdr, pack + FC_TERRAIN, AR_TERRAIN_HDR);
    memcpy(h, pack + FC_TERRAIN + AR_TERRAIN_HDR, AR_TN * AR_TN);
    memcpy(c, pack + FC_TERRAIN + AR_TERRAIN_HDR + AR_TN * AR_TN, AR_TN * AR_TN);
    return FEED_OK;
}

/* The pack has one patch: its grid repeats every AR_TN cells. */
enum feed_err feed_strip(const char *key, s16 x, s16 y, u16 w, u16 h, u8 *buf)
{
    const u8 *hm = fake_cockpit_rom + FC_TERRAIN + AR_TERRAIN_HDR, *cm = hm + AR_TN * AR_TN;

    (void)key;
    for (u16 r = 0; r < h; r++)
        for (u16 k = 0; k < w; k++) {
            u16 i = (((y + r) & (AR_TN - 1)) << 7) | ((x + k) & (AR_TN - 1));
            buf[r * w + k] = hm[i];
            buf[w * h + r * w + k] = cm[i];
        }
    return FEED_OK;
}

/* The recording plays in real time from the first request, looping; age is the time since its frame. */
enum feed_err feed_track(const u8 icao[3], const char *key, struct ar_track *t)
{
    static u32 start;
    const u8 *pack = fake_cockpit_rom;
    u16 frames = *(const u16 *)(pack + 2);
    const u8 *f = pack + FC_TERRAIN + AR_TERRAIN_SIZE, *last = f, *best = f;

    (void)key;
    if (!start) start = vtimer;
    for (u16 i = 0; i < frames; i++) {      /* the last frame, for the loop length */
        last = f;
        f += 4 + *(const u16 *)f * FC_ENTRY;
    }
    u16 span = *(const u16 *)(last + 2) + 40;
    u16 now = (u16)((vtimer - start) * 10 / 60) % span;
    f = pack + FC_TERRAIN + AR_TERRAIN_SIZE;
    for (u16 i = 0; i < frames; i++) {
        if (*(const u16 *)(f + 2) <= now) best = f;
        f += 4 + *(const u16 *)f * FC_ENTRY;
    }
    memset(t, 0, sizeof(*t));
    u16 n = *(const u16 *)best;
    for (const u8 *e = best + 4; n--; e += FC_ENTRY)
        if (e[0] == icao[0] && e[1] == icao[1] && e[2] == icao[2]) {
            memcpy(t, e + 4, AR_TRACK_SIZE);
            t->age += (now - *(const u16 *)(best + 2)) / 10;
            break;
        }
    return FEED_OK;
}

enum feed_err feed_find_relay(void) { return FEED_OK; }
bool feed_reconnect_due(void) { return FALSE; }
enum feed_err feed_reconnect(void) { return FEED_OK; }
const char *feed_relay_url(void) { return ""; }
const char *feed_relay_host(void) { return ""; }

#endif
