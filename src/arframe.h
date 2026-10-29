#ifndef ARFRAME_H
#define ARFRAME_H

/* The relay <-> cartridge contract (lode/architecture/contract.md): the compact traffic frame
 * (spec: lode/architecture/frame-format.md), the aircraft detail record, the HTTP requests, and
 * mDNS discovery. relay/contract_test.go parses this file and checks the relay against every AR_*
 * item here, so a change on either side that isn't mirrored fails CI. Keep each item on one line.
 * Any change here must bump AR_VER (relay/contract.lock enforces it; `make contract-lock` after).
 * A relay and a cart only talk at the same AR_VER: the relay answers others with
 * AR_HTTP_MISMATCH, and the cart says so on screen.
 * Big-endian, naturally aligned: read it in place from a word-aligned buffer. */
#include <genesis.h>

#define AR_VER          2
#define AR_REC_SIZE_V1  24
#define AR_MAX_RECS     96
#define AR_HDR_SIZE     16
#define AR_FRAME_MAX    (AR_HDR_SIZE + AR_MAX_RECS * AR_REC_SIZE_V1)   /* 2320 */

/* struct ar_hdr.source */
#define AR_SRC_ADSBFI   1
#define AR_SRC_ADSBLOL  2
#define AR_SRC_OPENSKY  3

/* struct ar_hdr.hflags */
#define AR_H_TRUNCATED      0x01
#define AR_H_UPSTREAM_ERR   0x02

/* struct ar_rec.flags */
#define AR_F_GROUND     0x01
#define AR_F_EMERG      0x02
#define AR_F_STALE      0x04
#define AR_F_ALT_UNK    0x08
#define AR_F_GS_UNK     0x10
#define AR_F_TRK_UNK    0x20
#define AR_F_VR_UNK     0x40

#define AR_SQUAWK_NONE  0xFFFF
#define AR_CAT_NONE     0xFF

/* HTTP: GET <relay base URL> AR_FRAME_PATH AR_FRAME_QUERY (lat, lon as degrees; range nm; max records;
 * AR_VER). The relay answers a request without v=AR_VER with AR_HTTP_MISMATCH. */
#define AR_FRAME_PATH   "/v1/frame"
#define AR_FRAME_QUERY  "?lat=%s&lon=%s&range=%u&max=%u&v=%u"
#define AR_HTTP_MISMATCH 409

/* GET <relay> AR_DETAIL_PATH AR_DETAIL_QUERY (icao as 6 hex digits; AR_VER): one struct ar_detail.
 * An aircraft the relay doesn't know gets a record without AR_D_FOUND. */
#define AR_DETAIL_PATH  "/v1/detail"
#define AR_DETAIL_QUERY "?icao=%02x%02x%02x&v=%u"
#define AR_DETAIL_SIZE  96

/* struct ar_detail.dflags: which values are known */
#define AR_D_FOUND      0x01
#define AR_D_GEOM_ALT   0x02
#define AR_D_SEL_ALT    0x04
#define AR_D_SEL_HDG    0x08
#define AR_D_QNH        0x10
#define AR_D_YEAR       0x20
#define AR_D_SEEN       0x40

/* mDNS discovery (lode/net/discovery.md): legacy-unicast PTR query from AR_MDNS_SRC_PORT */
#define AR_MDNS_SERVICE  "_mwradar._tcp.local"
#define AR_MDNS_QUERY_ID 0x4D57
#define AR_MDNS_SRC_PORT 5354

struct ar_hdr {
    char magic[2];      /* "AR" */
    u8 ver;
    u8 count;
    u16 rec_size;       /* >= 24; skip the tail of larger (newer) records */
    u16 seq;
    u32 time;           /* unix seconds of the upstream snapshot */
    u16 range_nm;
    u8 source;
    u8 hflags;
};

struct ar_rec {
    u8 icao[3];
    u8 flags;
    s16 x;              /* east of home, 1/32 nm */
    s16 y;              /* north of home, 1/32 nm */
    s16 alt;            /* 25 ft units; tag shows alt >> 2 */
    u16 squawk;         /* octal digits as nibbles: 0x7700 */
    u8 gs;              /* 2 kt units */
    u8 track;           /* 1/256 circle */
    s8 vrate;           /* 64 ft/min units */
    u8 cat;             /* A3 -> 0x03, B1 -> 0x11 */
    char cs[8];         /* space padded, not NUL terminated */
};

/* Per-aircraft detail for the pop-up (lode/ui/detail-popup.md); the live values stay in ar_rec. */
struct ar_detail {
    char magic[2];      /* "AD" */
    u8 ver;             /* AR_VER */
    u8 dflags;          /* AR_D_* */
    u8 icao[3];
    u8 cat;             /* as ar_rec.cat */
    s16 geom_alt;       /* GPS altitude, 25 ft units */
    s16 sel_alt;        /* autopilot selected altitude, 25 ft units */
    u16 sel_hdg;        /* autopilot selected heading, degrees */
    u16 qnh;            /* altimeter setting, 0.1 hPa */
    u16 year;           /* year built */
    char reg[8];        /* registration; strings are space padded, not NUL terminated */
    char type[4];       /* ICAO type designator, e.g. B38M */
    u16 seen;           /* seconds since the last message */
    char desc[32];      /* e.g. BOEING 737 MAX 8 */
    char owner[32];     /* owner / operator */
};

/* Layout is the wire format: any padding or reordering is a protocol break. */
#define AR_AT(t, f, off) _Static_assert(__builtin_offsetof(struct t, f) == (off), #t "." #f)
_Static_assert(sizeof(struct ar_hdr) == AR_HDR_SIZE, "ar_hdr must be 16 bytes");
_Static_assert(sizeof(struct ar_rec) == AR_REC_SIZE_V1, "ar_rec must be 24 bytes");
_Static_assert(sizeof(struct ar_detail) == AR_DETAIL_SIZE, "ar_detail must be 96 bytes");
AR_AT(ar_hdr, magic, 0); AR_AT(ar_hdr, ver, 2); AR_AT(ar_hdr, count, 3);
AR_AT(ar_hdr, rec_size, 4); AR_AT(ar_hdr, seq, 6); AR_AT(ar_hdr, time, 8);
AR_AT(ar_hdr, range_nm, 12); AR_AT(ar_hdr, source, 14); AR_AT(ar_hdr, hflags, 15);
AR_AT(ar_rec, icao, 0); AR_AT(ar_rec, flags, 3); AR_AT(ar_rec, x, 4); AR_AT(ar_rec, y, 6); AR_AT(ar_rec, alt, 8);
AR_AT(ar_rec, squawk, 10); AR_AT(ar_rec, gs, 12); AR_AT(ar_rec, track, 13);
AR_AT(ar_rec, vrate, 14); AR_AT(ar_rec, cat, 15); AR_AT(ar_rec, cs, 16);
AR_AT(ar_detail, magic, 0); AR_AT(ar_detail, ver, 2); AR_AT(ar_detail, dflags, 3); AR_AT(ar_detail, icao, 4);
AR_AT(ar_detail, cat, 7); AR_AT(ar_detail, geom_alt, 8); AR_AT(ar_detail, sel_alt, 10);
AR_AT(ar_detail, sel_hdg, 12); AR_AT(ar_detail, qnh, 14); AR_AT(ar_detail, year, 16); AR_AT(ar_detail, reg, 18);
AR_AT(ar_detail, type, 26); AR_AT(ar_detail, seen, 30); AR_AT(ar_detail, desc, 32); AR_AT(ar_detail, owner, 64);
#undef AR_AT

#endif
