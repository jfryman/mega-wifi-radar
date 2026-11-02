/* Fake feed for emulators (build with FAKE_FEED): cycles through frames
 * recorded around KBNA (res/fake/kbna_seq.bin, made by `relay -pack`), with their aircraft's
 * pop-up details (res/fake/kbna_detail.bin) and, for cockpit mode, one terrain patch around KBNA with every
 * aircraft's track in it (res/fake/kbna_cockpit.bin, `relay -pack-cockpit`). A flyover notice about the nearest
 * airborne aircraft turns up NOTICE_FIRST seconds after the first frame, then every NOTICE_EVERY.
 * Video maps from res/fake/maps.bin (`relay -pack-map`, around KBNA). Weather comes from res/fake/wx.bin (`relay -pack-wx`): a storm recorded around KMCO, shown around home.
 * Built with FAKE_EMERG (`make sim SIM_FLAGS=-DFAKE_EMERG`), the third aircraft squawks 7700 from the second
 * frame on, for the emergency alert (alert.c). */
#include "feed.h"

#ifdef FAKE_FEED
#include "fmt.h"
#include "resources.h"
#include "settings.h"

#define NOTICE_FIRST    20
#define NOTICE_EVERY    90

static const struct ar_hdr *last_frame;
static u32 notice_at;                       /* vtimer of the next notice */
static u16 notice_id;

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
#ifdef FAKE_EMERG
    static u16 buf[AR_FRAME_MAX / 2];
    static u16 polls;
    memcpy(buf, h, st->bytes);
    h = (const struct ar_hdr *)buf;
    if (++polls >= 2 && h->count > 2) {
        struct ar_rec *r = (struct ar_rec *)feed_rec(h, 2);
        r->squawk = 0x7700;
        r->flags |= AR_F_EMERG;
    }
#endif
    *hdr = h;
    last_frame = h;
    if (!notice_at) notice_at = vtimer + NOTICE_FIRST * 60UL;
    return FEED_OK;
}

bool feed_notice_waiting(void) { return notice_at && (s32)(vtimer - notice_at) >= 0; }

/* As the relay composes them (relay/notice.go): headline + callsign, operator + type, altitude + speed. */
enum feed_err feed_notice(const struct ar_notice **n)
{
    static struct ar_notice nt;
    static const char *const heads[] = { "OVERHEAD!", "LOOK UP!", "INCOMING!", "FLYBY!" };
    const struct ar_detail *d;
    char line[3][AR_NOTICE_COLS + 8], cs[9], own[12], typ[5], alt[16];

    notice_at = vtimer + NOTICE_EVERY * 60UL;
    memset(&nt, 0, sizeof(nt));
    nt.magic[0] = 'A'; nt.magic[1] = 'N'; nt.ver = AR_VER;
    const struct ar_rec *r = NULL;
    for (u16 i = 0; last_frame && i < last_frame->count && !r; i++) {   /* frames are nearest first */
        const struct ar_rec *c = feed_rec(last_frame, i);
        if (!(c->flags & (AR_F_GROUND | AR_F_ALT_UNK)) && c->alt > 0) r = c;
    }
    if (!r) { *n = &nt; return FEED_OK; }
    nt.id = ++notice_id;
    nt.nflags = AR_N_ICAO;
    memcpy(nt.icao, r->icao, 3);
    nt.secs = 15;
    memcpy(cs, r->cs, 8);
    cs[8] = 0;
    for (u16 i = 8; i && cs[i - 1] == ' '; i--) cs[i - 1] = 0;
    sprintf(line[0], "%-10s %s", heads[notice_id & 3], cs);
    own[0] = typ[0] = 0;
    if (feed_detail(r->icao, &d) == FEED_OK) {
        u16 k = 0;
        while (k < 11 && d->owner[k] != ' ') { own[k] = d->owner[k]; k++; }
        own[k] = 0;
        memcpy(typ, d->type, 4);
        typ[4] = 0;
    }
    sprintf(line[1], "%-12s%s", own, typ);
    line[2][0] = 0;
    if (!(r->flags & (AR_F_GROUND | AR_F_ALT_UNK)) && r->alt > 0) {
        fmt_thousands(alt, (s32)r->alt * 25);
        sprintf(line[2], "%sFT ", alt);
    }
    if (!(r->flags & AR_F_GS_UNK)) sprintf(line[2] + strlen(line[2]), "%uKT", r->gs * 2);
    for (u16 l = 0; l < 3; l++) {
        u16 k = 0;
        for (; k < AR_NOTICE_COLS && line[l][k]; k++) nt.text[l * AR_NOTICE_COLS + k] = line[l][k];
        for (; k < AR_NOTICE_COLS; k++) nt.text[l * AR_NOTICE_COLS + k] = ' ';
    }
    *n = &nt;
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

/* "FW", u16 count, then count x struct ar_wx, one per range (res/fake/wx.bin, `relay -pack-wx`). */
bool feed_wx_waiting(void) { return FALSE; }

enum feed_err feed_wx(const struct ar_wx **w)
{
    const u8 *pack = fake_wx_rom;
    u16 count = *(const u16 *)(pack + 2);
    const struct ar_wx *r = (const struct ar_wx *)(pack + 4);

    for (u16 i = 0; i < count; i++, r++)
        if (r->range_nm == settings_range_nm()) {
            *w = r;
            return FEED_OK;
        }
    return FEED_ERR_HTTP;
}

/* "FM", u16 count, then per range: u16 length, the /v1/map body, padded even (res/fake/maps.bin, `relay -pack-map`). */
enum feed_err feed_map(u8 *buf, u16 size)
{
    const u8 *p = fake_map_rom + 4;
    u16 count = *(const u16 *)(fake_map_rom + 2);

    for (u16 i = 0; i < count; i++) {
        u16 len = *(const u16 *)p;
        const struct ar_map *m = (const struct ar_map *)(p + 2);
        if (m->range_nm == settings_range_nm() && len <= size) {
            memcpy(buf, m, len);
            return FEED_OK;
        }
        p += 2 + ((len + 1) & ~1);
    }
    return FEED_ERR_HTTP;
}

/* res/fake/log.bin (`relay -pack-log`): the recorded KBNA snapshots replayed into a logbook. */
enum feed_err feed_log(const struct ar_log **l)
{
    *l = (const struct ar_log *)fake_log_rom;
    return FEED_OK;
}

enum feed_err feed_find_relay(void) { return FEED_OK; }
bool feed_reconnect_due(void) { return FALSE; }
enum feed_err feed_reconnect(void) { return FEED_OK; }
const char *feed_relay_url(void) { return ""; }
const char *feed_relay_host(void) { return ""; }

#endif
