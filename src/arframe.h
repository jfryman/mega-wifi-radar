#ifndef ARFRAME_H
#define ARFRAME_H

/* The relay <-> cartridge contract (lode/architecture/contract.md): the compact traffic frame
 * (spec: lode/architecture/frame-format.md), the aircraft detail record, cockpit mode's terrain patch
 * and track (lode/ui/cockpit.md), flyover notices (lode/plans/flyover-notices.md), the HTTP requests, and mDNS discovery. relay/contract_test.go parses this file and checks the relay against every AR_*
 * item here, so a change on either side that isn't mirrored fails CI. Keep each item on one line.
 * Any change here must bump AR_VER (relay/contract.lock enforces it; `make contract-lock` after).
 * A relay and a cart only talk at the same AR_VER: the relay answers others with
 * AR_HTTP_MISMATCH, and the cart says so on screen.
 * Big-endian, naturally aligned: read it in place from a word-aligned buffer. */
#include <genesis.h>

#define AR_VER          4
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
#define AR_H_NOTICE         0x04   /* a flyover notice newer than the request's n= is waiting: fetch AR_NOTICE_PATH */

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
 * the id of the last flyover notice seen, 0 for none; AR_VER). The relay answers a request without v=AR_VER
 * with AR_HTTP_MISMATCH. */
#define AR_FRAME_PATH   "/v1/frame"
#define AR_FRAME_QUERY  "?lat=%s&lon=%s&range=%u&max=%u&n=%u&v=%u"
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

/* Cockpit mode. GET <relay> AR_TERRAIN_PATH AR_TERRAIN_QUERY (icao, AR_VER): a terrain patch centred on
 * the aircraft, its cell size picked from the aircraft's height above the ground. Body: struct ar_terrain,
 * then AR_TN * AR_TN height levels (0..63, one byte each), then AR_TN * AR_TN palette indices (4..15),
 * both row by row from the south edge, each row west to east (AR_TERRAIN_SIZE bytes in all). */
#define AR_TERRAIN_PATH  "/v1/terrain"
#define AR_TERRAIN_QUERY "?icao=%02x%02x%02x&v=%u"
#define AR_TN            128
#define AR_TERRAIN_HDR   104
#define AR_TERRAIN_SIZE  (AR_TERRAIN_HDR + 2 * AR_TN * AR_TN)
#define AR_LEVELS        64
#define AR_KEY_LEN       56

/* struct ar_terrain.tflags */
#define AR_T_FLAT        0x01   /* no elevation data for this area: flat ground */

/* GET <relay> AR_TRACK_PATH AR_TRACK_QUERY (icao, the patch's key, the last notice id seen, AR_VER): one
 * struct ar_track, placed in that patch. An aircraft the relay doesn't know gets a record without AR_K_FOUND. */
#define AR_TRACK_PATH    "/v1/track"
#define AR_TRACK_QUERY   "?icao=%02x%02x%02x&p=%s&n=%u&v=%u"
#define AR_TRACK_SIZE    40

/* GET <relay> AR_STRIP_PATH AR_STRIP_QUERY (the patch's key, x, y, w, h in cells, AR_VER): w * h height levels,
 * then w * h palette indices, for a rectangle of the patch's cell grid, which goes on past its AR_TN cells at
 * the same scale and colouring (rows from the south, each row west to east). w, h <= AR_TN, w * h <=
 * AR_STRIP_CELLS. Cockpit mode streams strips into its wrapping AR_TN x AR_TN buffer as the aircraft moves. */
#define AR_STRIP_PATH    "/v1/strip"
#define AR_STRIP_QUERY   "?p=%s&x=%d&y=%d&w=%u&h=%u&v=%u"
#define AR_STRIP_CELLS   1024

/* struct ar_track.kflags */
#define AR_K_FOUND       0x01
#define AR_K_GROUND      0x02
#define AR_K_EMERG       0x04
#define AR_K_ALT_UNK     0x08   /* z is the ground under the aircraft */
#define AR_K_NOTICE      0x10   /* as AR_H_NOTICE, in cockpit mode */

/* Flyover notices (lode/plans/flyover-notices.md): a plane over the owner's house, from the relay's MQTT feed.
 * GET <relay> AR_NOTICE_PATH AR_NOTICE_QUERY (the last notice id seen, 0 for none; AR_VER): one struct
 * ar_notice, the oldest waiting one newer than that id, or one with id 0 if none is waiting any more.
 * Ids wrap: newer means (s16)(id - seen) > 0. */
#define AR_NOTICE_PATH   "/v1/notice"
#define AR_NOTICE_QUERY  "?after=%u&v=%u"
#define AR_NOTICE_SIZE   88
#define AR_NOTICE_COLS   26     /* characters per line; text holds 3 lines */

/* struct ar_notice.nflags */
#define AR_N_ICAO        0x01   /* icao is the aircraft in the feed */
#define AR_N_EMERG       0x02

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

/* Cockpit terrain patch header (lode/ui/cockpit.md). Cell (col, row) is centred cell_m * (col + 0.5) east and
 * cell_m * (row + 0.5) north of the patch's south-west corner. Height level h is base_m + h * level_cm / 100
 * metres above sea level. */
struct ar_terrain {
    char magic[2];      /* "AT" */
    u8 ver;             /* AR_VER */
    u8 tflags;          /* AR_T_* */
    u16 n;              /* cells per side: AR_TN */
    u16 cell_m;         /* metres per cell */
    u16 level_cm;       /* centimetres per height level */
    s16 base_m;         /* elevation of level 0 */
    u8 hmax;            /* highest level in the patch */
    u8 rsv;
    u16 pal[16];        /* the terrain palette (0x0BGR): 1-3 sky, deep to pale; 4-15 ground */
    char key[56];       /* AR_KEY_LEN: the patch, for /v1/track's p= (NUL-terminated, URL-safe) */
    u8 pad[2];
};

/* One aircraft, placed in a terrain patch: for the cart's camera, plus a few values for its HUD. */
struct ar_track {
    char magic[2];      /* "AK" */
    u8 ver;             /* AR_VER */
    u8 kflags;          /* AR_K_* */
    s32 x;              /* east of the patch's west edge, 1/256 cell */
    s32 y;              /* north of the patch's south edge, 1/256 cell */
    s32 z;              /* above the patch's level 0, 1/256 level */
    s16 vx;             /* 1/256 cell per second */
    s16 vy;
    s16 vz;             /* 1/256 level per second */
    u16 heading;        /* true heading (or track), 65536 per turn */
    s8 bank;            /* degrees, right wing down positive (reported or estimated) */
    u8 age;             /* seconds since the position was reported */
    s16 alt;            /* 25 ft units, as ar_rec.alt */
    u16 gs;             /* knots */
    s16 vrate;          /* ft/min */
    char cs[8];         /* space padded */
};

/* A flyover notice: three lines composed by the relay, ready to print. */
struct ar_notice {
    char magic[2];      /* "AN" */
    u8 ver;             /* AR_VER */
    u8 nflags;          /* AR_N_* */
    u16 id;             /* 0: no notice */
    u8 icao[3];         /* with AR_N_ICAO */
    u8 secs;            /* how long to show it */
    char text[78];      /* 3 lines of AR_NOTICE_COLS, space padded, upper-case ASCII */
};

/* Layout is the wire format: any padding or reordering is a protocol break. */
#define AR_AT(t, f, off) _Static_assert(__builtin_offsetof(struct t, f) == (off), #t "." #f)
_Static_assert(sizeof(struct ar_hdr) == AR_HDR_SIZE, "ar_hdr must be 16 bytes");
_Static_assert(sizeof(struct ar_rec) == AR_REC_SIZE_V1, "ar_rec must be 24 bytes");
_Static_assert(sizeof(struct ar_detail) == AR_DETAIL_SIZE, "ar_detail must be 96 bytes");
_Static_assert(sizeof(struct ar_terrain) == AR_TERRAIN_HDR, "ar_terrain must be 104 bytes");
_Static_assert(sizeof(struct ar_track) == AR_TRACK_SIZE, "ar_track must be 40 bytes");
_Static_assert(sizeof(struct ar_notice) == AR_NOTICE_SIZE, "ar_notice must be 88 bytes");
_Static_assert(sizeof(((struct ar_notice *)0)->text) == 3 * AR_NOTICE_COLS, "ar_notice.text is 3 lines");
_Static_assert(sizeof(((struct ar_terrain *)0)->key) == AR_KEY_LEN, "ar_terrain.key is AR_KEY_LEN");
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
AR_AT(ar_terrain, magic, 0); AR_AT(ar_terrain, ver, 2); AR_AT(ar_terrain, tflags, 3); AR_AT(ar_terrain, n, 4);
AR_AT(ar_terrain, cell_m, 6); AR_AT(ar_terrain, level_cm, 8); AR_AT(ar_terrain, base_m, 10);
AR_AT(ar_terrain, hmax, 12); AR_AT(ar_terrain, rsv, 13); AR_AT(ar_terrain, pal, 14); AR_AT(ar_terrain, key, 46);
AR_AT(ar_terrain, pad, 102);
AR_AT(ar_track, magic, 0); AR_AT(ar_track, ver, 2); AR_AT(ar_track, kflags, 3); AR_AT(ar_track, x, 4);
AR_AT(ar_track, y, 8); AR_AT(ar_track, z, 12); AR_AT(ar_track, vx, 16); AR_AT(ar_track, vy, 18);
AR_AT(ar_track, vz, 20); AR_AT(ar_track, heading, 22); AR_AT(ar_track, bank, 24); AR_AT(ar_track, age, 25);
AR_AT(ar_track, alt, 26); AR_AT(ar_track, gs, 28); AR_AT(ar_track, vrate, 30); AR_AT(ar_track, cs, 32);
AR_AT(ar_notice, magic, 0); AR_AT(ar_notice, ver, 2); AR_AT(ar_notice, nflags, 3); AR_AT(ar_notice, id, 4);
AR_AT(ar_notice, icao, 6); AR_AT(ar_notice, secs, 9); AR_AT(ar_notice, text, 10);
#undef AR_AT

#endif
