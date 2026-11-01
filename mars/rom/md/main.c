/* 32X ROM, 68000 side: the I/O processor (lode/mars/summary.md). After boot.s has started the SH-2s, it serves
 * the slave SH-2's requests over the COMM link (lode/mars/link.md) with the Genesis ROM's own feed and network
 * code: src/feed_relay.c + src/net.c on the cart, src/feed_fake.c (the recorded KBNA demo) in the emulator
 * build (FAKE_FEED). Between requests it keeps the pad in COMM14 and pumps the MegaWiFi driver. */
#include "genesis.h"
#include "../link.h"
#include "link_md.h"
#include "feed.h"
#include "settings.h"
#ifndef FAKE_FEED
#include "net.h"
#include "ext/mw/megawifi.h"
#endif

void vdp_init(void);
extern const char *const app_version;

/* src/feed_relay.c reads these; the slave SH-2 owns the real ones and pushes them (LK_SETTINGS). */
struct settings settings;
const u16 range_steps[RANGE_COUNT] = { 5, 10, 25, 50, 100 };

static u8 req[256] __attribute__((aligned(4)));
static u8 out[256] __attribute__((aligned(4)));
/* a terrain patch: the header then the height and colour maps, as /v1/terrain's body */
static struct {
    struct ar_terrain hdr;
    u8 h[AR_TN * AR_TN];
    u8 c[AR_TN * AR_TN];
} terrain;
static u8 strip[2 * AR_STRIP_CELLS];

static u16 rd16(const u8 *p) { return p[0] << 8 | p[1]; }
#ifndef FAKE_FEED
static u32 rd32(const u8 *p) { return (u32)rd16(p) << 16 | rd16(p + 2); }

/* "ssid\0pass\0": pointers into req */
static const char *second(const char *s) { return s + strlen(s) + 1; }
#endif

static u16 rom_checksum(void)
{
    const volatile u16 *p = (const volatile u16 *)0x88018E;     /* the header, through the 32X window */
    __asm__("" : "+a"(p));
    return *p;
}

static void serve(u8 cmd, u16 n)
{
    enum feed_err e;

    switch (cmd) {
    case LK_HELLO: {
#ifdef FAKE_FEED
        out[0] = 1;
#else
        out[0] = 0;
#endif
        out[1] = 0;
        u16 sum = rom_checksum();
        out[2] = sum >> 8;
        out[3] = sum;
        strcpy((char *)out + 4, app_version);
        lk_reply(0, out, 4 + strlen(app_version) + 1);
        break;
    }
    case LK_SETTINGS:
        if (n == sizeof(settings)) memcpy(&settings, req, sizeof(settings));
        lk_reply(0, NULL, 0);
        break;
    case LK_FEED_FETCH: {
        const struct ar_hdr *h = NULL;
        struct feed_stats st;
        e = feed_fetch(&h, &st);
        u16 len = e == FEED_OK && h ? AR_HDR_SIZE + h->count * h->rec_size : 0;
        lk_reply_begin(e, sizeof(st) + len);
        lk_reply_data(&st, sizeof(st));
        if (len) lk_reply_data(h, len);
        lk_reply_end();
        break;
    }
    case LK_FEED_DETAIL: {
        const struct ar_detail *d = NULL;
        e = feed_detail(req, &d);
        lk_reply(e, d, e == FEED_OK && d ? sizeof(*d) : 0);
        break;
    }
    case LK_FEED_TERRAIN:
        e = feed_terrain(req, &terrain.hdr, terrain.h, terrain.c);
        lk_reply(e, &terrain, e == FEED_OK ? sizeof(terrain) : 0);
        break;
    case LK_FEED_TRACK: {
        static struct ar_track t;
        req[4 + AR_KEY_LEN - 1] = 0;
        e = feed_track(req, (const char *)req + 4, &t);
        lk_reply(e, &t, e == FEED_OK ? sizeof(t) : 0);
        break;
    }
    case LK_FEED_STRIP: {
        s16 x = rd16(req), y = rd16(req + 2);
        u16 w = rd16(req + 4), h = rd16(req + 6);
        req[8 + AR_KEY_LEN - 1] = 0;
        if (w * h > AR_STRIP_CELLS) { lk_reply(FEED_ERR_SIZE, NULL, 0); break; }
        e = feed_strip((const char *)req + 8, x, y, w, h, strip);
        lk_reply(e, strip, e == FEED_OK ? 2 * w * h : 0);
        break;
    }
    case LK_FIND_RELAY: lk_reply(feed_find_relay(), NULL, 0); break;
    case LK_RECONNECT_DUE: lk_reply(feed_reconnect_due(), NULL, 0); break;
    case LK_RECONNECT: lk_reply(feed_reconnect(), NULL, 0); break;
    case LK_RELAY_URL: {
        const char *u = feed_relay_url(), *h = feed_relay_host();
        u16 a = strlen(u) + 1, b = strlen(h) + 1;
        lk_reply_begin(0, a + b);
        lk_reply_data(u, a);
        lk_reply_data(h, b);
        lk_reply_end();
        break;
    }
#ifndef FAKE_FEED
    case LK_NET_INIT: net_init(); lk_reply(0, NULL, 0); break;
    case LK_NET_DETECT: {
        struct net_module_info mod;
        enum net_err ne = net_detect(&mod);
        out[0] = mod.major;
        out[1] = mod.minor;
        strncpy((char *)out + 2, ne == NET_OK ? mod.variant : "", 40);
        out[42] = 0;
        lk_reply(ne, out, 2 + strlen((char *)out + 2) + 1);
        break;
    }
    case LK_NET_ENSURE_AP: {
        const char *ssid = (const char *)req;
        lk_reply(net_ensure_ap(ssid[0] ? ssid : NULL, second(ssid)), NULL, 0);
        break;
    }
    case LK_NET_JOIN: {
        enum net_err ne = net_join(out);
        lk_reply(ne, out, 4);
        break;
    }
    case LK_NET_SCAN: {
        static struct net_ap aps[14];
        u16 max = rd16(req);
        if (max > 14) max = 14;
        s16 k = net_scan(aps, max);
        u16 cnt = k < 0 ? 0 : k;
        out[0] = (u16)k >> 8;
        out[1] = k;
        lk_reply_begin(0, 2 + cnt * sizeof(struct net_ap));
        lk_reply_data(out, 2);
        lk_reply_data(aps, cnt * sizeof(struct net_ap));
        lk_reply_end();
        break;
    }
    case LK_NET_CONNECT: {
        const char *ssid = (const char *)req;
        enum net_err ne = net_connect(ssid, second(ssid), out);
        out[4] = net_save_step;
        lk_reply(ne, out, 5);
        break;
    }
    case LK_NET_SSID:
        net_current_ssid((char *)out);
        lk_reply(0, out, strlen((char *)out) + 1);
        break;
    case LK_NET_REJOIN: lk_reply(net_rejoin_needed(), NULL, 0); break;
    case LK_FLASH_READ: {
        u32 addr = rd32(req);
        u16 len = rd16(req + 4);
        u8 *p = mw_flash_read(addr, len);
        lk_reply(p ? 0 : 1, p, p ? len : 0);
        break;
    }
    case LK_FLASH_ERASE: lk_reply(mw_flash_sector_erase(rd16(req)), NULL, 0); break;
    case LK_FLASH_WRITE: lk_reply(mw_flash_write(rd32(req), req + 4, n - 4), NULL, 0); break;
    case LK_AP_DISASSOC: lk_reply(mw_ap_disassoc(), NULL, 0); break;
    case LK_FACTORY: lk_reply(mw_factory_settings(), NULL, 0); break;
#endif
    default:
        lk_reply(0xFF, NULL, 0);           /* not in this build (e.g. Wi-Fi in the emulator) */
        break;
    }
}

int main(void)
{
    vdp_init();
    lk_init();
    for (;;) {
        u8 cmd;
        extern u16 md_stage;
        md_stage = 1;
        while (!lk_request_waiting()) { md_tick(); md_pump(); }
        u16 n = lk_recv(&cmd, req, sizeof(req));
        md_stage = 4;
        serve(cmd, n);
    }
}

void md_hard_reset(void)
{
    for (;;) md_tick();                     /* a 32X can't reset itself from software: power cycle */
}
