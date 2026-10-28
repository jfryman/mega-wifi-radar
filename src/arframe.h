#ifndef ARFRAME_H
#define ARFRAME_H

/* Compact traffic frame, v1. Spec: lode/architecture/frame-format.md.
 * Big-endian, naturally aligned: read it in place from a word-aligned buffer. */
#include <genesis.h>

#define AR_VER          1
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

/* Layout is the wire format: any padding or reordering is a protocol break. */
#define AR_AT(t, f, off) _Static_assert(__builtin_offsetof(struct t, f) == (off), #t "." #f)
_Static_assert(sizeof(struct ar_hdr) == AR_HDR_SIZE, "ar_hdr must be 16 bytes");
_Static_assert(sizeof(struct ar_rec) == AR_REC_SIZE_V1, "ar_rec must be 24 bytes");
AR_AT(ar_hdr, rec_size, 4); AR_AT(ar_hdr, seq, 6); AR_AT(ar_hdr, time, 8);
AR_AT(ar_hdr, range_nm, 12); AR_AT(ar_hdr, source, 14); AR_AT(ar_hdr, hflags, 15);
AR_AT(ar_rec, flags, 3); AR_AT(ar_rec, x, 4); AR_AT(ar_rec, y, 6); AR_AT(ar_rec, alt, 8);
AR_AT(ar_rec, squawk, 10); AR_AT(ar_rec, gs, 12); AR_AT(ar_rec, track, 13);
AR_AT(ar_rec, vrate, 14); AR_AT(ar_rec, cat, 15); AR_AT(ar_rec, cs, 16);
#undef AR_AT

#endif
