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
    FEED_ERR_FORMAT,     /* bad magic / record size / length */
    FEED_ERR_VERSION,    /* the relay is at another contract version (AR_VER): not this cart's release */
    FEED_ERR_WIFI,       /* reconnect: module not found or Wi-Fi join failed */
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

/* Blocking fetch of the pop-up detail for one aircraft (lode/ui/detail-popup.md). On FEED_OK,
 * *d is valid until the next feed_detail(); without AR_D_FOUND the relay doesn't know it. */
enum feed_err feed_detail(const u8 icao[3], const struct ar_detail **d);

/* Cockpit mode (lode/ui/cockpit.md), blocking. feed_terrain() streams /v1/terrain into hdr, h and c
 * (AR_TN * AR_TN bytes each, e.g. in the mode overlay): a patch centred on the aircraft. feed_track() places
 * the aircraft in the patch with that key; without AR_K_FOUND the relay doesn't know it. */
enum feed_err feed_terrain(const u8 icao[3], struct ar_terrain *hdr, u8 *h, u8 *c);
enum feed_err feed_track(const u8 icao[3], const char *key, struct ar_track *t);
/* /v1/strip: w * h heights then w * h colours of the patch's grid from cell (x, y), into buf (2 * w * h). */
enum feed_err feed_strip(const char *key, s16 x, s16 y, u16 w, u16 h, u8 *buf);

/* Flyover notices (lode/plans/flyover-notices.md). Frames and tracks report whether a notice newer than the
 * last one seen is waiting (they send that id as n=); feed_notice() fetches it (blocking, one small GET). On
 * FEED_OK, *n is valid until the next call; id 0 means it was gone by then. */
bool feed_notice_waiting(void);
enum feed_err feed_notice(const struct ar_notice **n);

/* Weather (lode/ui/weather.md). Frames say when the relay's weather differs from the last one fetched (sent as
 * w=); feed_wx() fetches the cells for the home point at the current range (blocking, one small GET). On
 * FEED_OK, *w is valid until the next call. */
bool feed_wx_waiting(void);
enum feed_err feed_wx(const struct ar_wx **w);

/* Video maps (lode/ui/video-maps.md): the map for the home point at the current range into buf (size bytes, word
 * aligned): struct ar_map, then its segments (blocking, one small GET). */
enum feed_err feed_map(u8 *buf, u16 size);

/* Spotter's logbook (lode/ui/logbook.md): today's log for the home point (blocking, one small GET). It arrives in
 * the frame buffer: *l (struct ar_log, then its records and rare sightings) is valid until the next feed_fetch(), so
 * call it after the frame is ingested and render it at once. */
enum feed_err feed_log(const struct ar_log **l);

/* The relay in use (lode/net/discovery.md): settings.relay if one is typed in, else AUTO, found
 * over mDNS. feed_find_relay() picks it (blocking: up to ~3 s in AUTO): FEED_OK, FEED_ERR_CONFIG
 * (none found) or FEED_ERR_VERSION (found one at another contract version). */
enum feed_err feed_find_relay(void);
/* Recovery (lode/net/reconnect.md): due after RELAY_RETRIES failed requests in a row (any error but a
 * mismatch), with back-off between attempts. feed_reconnect() resets the module, rejoins Wi-Fi and finds
 * the relay again (blocking, ~3-8 s): FEED_OK, FEED_ERR_WIFI, or feed_find_relay()'s errors. */
bool feed_reconnect_due(void);
enum feed_err feed_reconnect(void);
const char *feed_relay_url(void);       /* "" until found */
const char *feed_relay_host(void);      /* the same without "http://", for display */
#define RELAY_RETRIES 3
const struct ar_rec *feed_rec(const struct ar_hdr *hdr, u16 i);
const char *feed_err_str(enum feed_err e);

#endif
