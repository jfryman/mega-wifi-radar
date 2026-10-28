#ifndef FEED_H
#define FEED_H

/* Traffic feed: where compact frames come from. The relay implementation
 * (feed_relay.c) fetches over MegaWiFi; a fake in-ROM source for emulators
 * will implement the same interface. */
#include <genesis.h>
#include "arframe.h"

enum feed_err {
    FEED_OK = 0,
    FEED_ERR_CONFIG,     /* no relay URL configured */
    FEED_ERR_HTTP,       /* request failed or non-200 */
    FEED_ERR_RECV,       /* body receive failed */
    FEED_ERR_SIZE,       /* body larger than AR_FRAME_MAX */
    FEED_ERR_FORMAT,     /* bad magic / version / record size / length */
};

struct feed_stats {
    s16 http_status;
    u16 bytes;
    u16 chunks;
    u16 frames;          /* VBlanks for the whole request */
};

/* Blocking fetch of one frame. On FEED_OK, *hdr points at a validated frame
 * that stays valid until the next feed_fetch(). Records start at feed_rec(i). */
enum feed_err feed_fetch(const struct ar_hdr **hdr, struct feed_stats *st);
const struct ar_rec *feed_rec(const struct ar_hdr *hdr, u16 i);
const char *feed_err_str(enum feed_err e);

#endif
