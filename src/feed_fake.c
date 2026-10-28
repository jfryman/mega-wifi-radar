/* Fake feed for emulators (build with FAKE_FEED): cycles through frames
 * recorded around KBNA (res/fake/kbna_seq.bin, made by `relay -pack`). */
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
#endif
