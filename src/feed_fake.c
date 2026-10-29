/* Fake feed for emulators (build with FAKE_FEED): cycles through frames
 * recorded around KBNA (res/fake/kbna_seq.bin, made by `relay -pack`), with their aircraft's
 * pop-up details (res/fake/kbna_detail.bin). */
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
enum feed_err feed_find_relay(void) { return FEED_OK; }
const char *feed_relay_url(void) { return ""; }
const char *feed_relay_host(void) { return ""; }

#endif
