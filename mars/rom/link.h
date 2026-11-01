/* The COMM link between the 68000 (I/O server) and the slave SH-2 (the app): registers, blocks, RPC commands.
 * lode/mars/link.md. Byte offsets from COMM0: $A15120 on the 68000, $20004020 on the SH-2s. */
#ifndef MARS_LINK_H
#define MARS_LINK_H

#define LK_SH_CTRL      0       /* slave: low byte = sequence / ack */
#define LK_SH_DATA      2       /* slave: 3 words, 2..6 */
#define LK_MD_CTRL      8       /* 68000: low byte = sequence / ack, high byte = LK_ST_* */
#define LK_MD_DATA      10      /* 68000: 2 words, 10..12 */
#define LK_PAD          14      /* 68000: pad 1 (BUTTON_*), every frame */

#define LK_SH_BLOCK     6       /* payload bytes per block */
#define LK_MD_BLOCK     4

#define LK_ST_BOOT      0x00    /* COMM8 high byte: the 68000 hasn't started serving yet */
#define LK_ST_READY     0x5A    /* serving */

#define LK_MAX_MSG      (4 + 104 + 2 * 128 * 128)   /* the biggest reply: a terrain patch (+ status) */

/* Requests: u8 cmd, u8 0, then args. Replies: u8 status (enum feed_err / net_err / 0-1), u8 0, then data. */
enum lk_cmd {
    LK_HELLO = 1,       /* -> u8 fake, u8 0, u16 rom_checksum, char version[] */
    LK_SETTINGS,        /* struct settings -> */
    LK_FEED_FETCH,      /* -> struct feed_stats, frame body */
    LK_FEED_DETAIL,     /* u8 icao[3] -> struct ar_detail */
    LK_FEED_TERRAIN,    /* u8 icao[3] -> struct ar_terrain, h[AR_TN^2], c[AR_TN^2] */
    LK_FEED_TRACK,      /* u8 icao[3], u8 0, char key[AR_KEY_LEN] -> struct ar_track */
    LK_FEED_STRIP,      /* s16 x, s16 y, u16 w, u16 h, char key[AR_KEY_LEN] -> 2 * w * h bytes */
    LK_FIND_RELAY,      /* -> */
    LK_RECONNECT_DUE,   /* -> (status = bool) */
    LK_RECONNECT,       /* -> */
    LK_RELAY_URL,       /* -> char url[], 0, char host[], 0 */
    LK_NET_INIT,        /* -> */
    LK_NET_DETECT,      /* -> u8 major, u8 minor, char variant[] */
    LK_NET_ENSURE_AP,   /* char ssid[], 0, char pass[], 0 -> (devcfg Wi-Fi seed) */
    LK_NET_JOIN,        /* -> u8 ip[4] */
    LK_NET_SCAN,        /* u16 max -> s16 n, n x struct net_ap */
    LK_NET_CONNECT,     /* char ssid[], 0, char pass[], 0 -> u8 ip[4], u8 save_step */
    LK_NET_SSID,        /* -> char ssid[] */
    LK_NET_REJOIN,      /* -> (status = bool) */
    LK_FLASH_READ,      /* u32 addr, u16 len -> bytes (status 1 = failed) */
    LK_FLASH_ERASE,     /* u16 sector -> */
    LK_FLASH_WRITE,     /* u32 addr, bytes -> */
    LK_AP_DISASSOC,     /* -> */
    LK_FACTORY,         /* -> */
    LK_HARD_RESET,      /* -> never returns: the 68000 resets the console */
};

#endif
