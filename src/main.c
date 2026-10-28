/* Network spike: detect module -> join AP -> poll the relay every 5 s ->
 * list the nearest aircraft. Proves Rev.C UART, Wi-Fi, HTTP and the v1 frame
 * end to end on hardware. */
#include <genesis.h>
#include "devcfg.h"
#include "feed.h"
#include "fmt.h"
#include "net.h"

#define COLS        40
#define POLL_FRAMES 300     /* ~5 s at 60 Hz, about one sweep */
#define STATUS_ROW  7
#define HDR_ROW     8
#define LIST_TOP    10
#define LIST_ROWS   (27 - LIST_TOP)
#define ATTR_ROW    27

static u16 line = 1;

static void say(const char *s)
{
    VDP_clearTextLine(line);
    VDP_drawText(s, 0, line++);
}

static void put_row(u16 row, u16 pal, const char *s)
{
    VDP_setTextPalette(pal);
    VDP_clearTextLine(row);
    VDP_drawText(s, 0, row);
    VDP_setTextPalette(PAL0);
}

static void fail(const char *s)
{
    put_row(line++, PAL1, s);
    while (TRUE) SYS_doVBlankProcess();
}

static const char *source_name(u8 src)
{
    switch (src) {
    case AR_SRC_ADSBFI:  return "adsb.fi";
    case AR_SRC_ADSBLOL: return "adsb.lol";
    case AR_SRC_OPENSKY: return "OpenSky";
    }
    return "?";
}

/* "SWA2468  019 172   7.6 ES": callsign, alt (hundreds of ft / GND), gs kt,
 * distance nm, then E = emergency, S = stale. */
static void show_rec(u16 row, const struct ar_rec *r)
{
    char buf[COLS + 1], alt[4], cs[9];
    s32 x = r->x, y = r->y;
    u16 d10 = ((u32)isqrt32(x * x + y * y) * 10) >> 5;     /* 1/32 nm -> 0.1 nm */

    memcpy(cs, r->cs, 8);
    cs[8] = 0;
    if (r->flags & AR_F_GROUND) strcpy(alt, "GND");
    else if (r->flags & AR_F_ALT_UNK) strcpy(alt, "---");
    else sprintf(alt, "%03d", (r->alt < 0 ? 0 : r->alt) >> 2);
    sprintf(buf, "%s %s %3u %3u.%u %c%c", cs, alt,
            (r->flags & AR_F_GS_UNK) ? 0 : r->gs * 2, d10 / 10, d10 % 10,
            (r->flags & AR_F_EMERG) ? 'E' : ' ', (r->flags & AR_F_STALE) ? 'S' : ' ');
    put_row(row, (r->flags & AR_F_EMERG) ? PAL1 : PAL0, buf);
}

static void poll_once(u16 n)
{
    const struct ar_hdr *h;
    struct feed_stats st;
    char buf[COLS + 1];
    enum feed_err e = feed_fetch(&h, &st);

    if (e != FEED_OK) {
        sprintf(buf, "#%u %s (HTTP %d, %uB)", n, feed_err_str(e), st.http_status, st.bytes);
        put_row(STATUS_ROW, PAL1, buf);
        return;
    }
    sprintf(buf, "#%u HTTP %d %uB %uch %uf", n, st.http_status, st.bytes, st.chunks, st.frames);
    put_row(STATUS_ROW, PAL0, buf);
    sprintf(buf, "AR v%u %u ac seq %u %s%s", h->ver, h->count, h->seq,
            (h->hflags & AR_H_TRUNCATED) ? "TRUNC " : "",
            (h->hflags & AR_H_UPSTREAM_ERR) ? "UPSTREAM ERR" : "");
    put_row(HDR_ROW, PAL0, buf);
    for (u16 i = 0; i < LIST_ROWS; i++) {
        if (i < h->count) show_rec(LIST_TOP + i, feed_rec(h, i));
        else VDP_clearTextLine(LIST_TOP + i);
    }
    sprintf(buf, "Data: %s", source_name(h->source));
    put_row(ATTR_ROW, PAL0, buf);
}

int main(bool hard)
{
    struct net_module_info mod;
    u8 ip[4];
    char buf[COLS + 1], lat[12], lon[12];

    (void)hard;
    line = 1;   /* soft reset keeps RAM: statics aren't re-initialised */
    PAL_setColor(PAL1 * 16 + 15, RGB24_TO_VDPCOLOR(0xFF4040));
    say("mega_wifi_radar relay spike (Rev.C)");
    fmt_deg_e4(lat, devcfg_home_lat_e4);
    fmt_deg_e4(lon, devcfg_home_lon_e4);
    sprintf(buf, "Home %s %s %s %unm", devcfg_home_icao, lat, lon, devcfg_range_nm);
    say(buf);
    if (!devcfg_relay_url) fail("Set DEV_RELAY_URL in src/dev_config.h");

    say("Detecting module...");
    net_init();
    if (net_detect(&mod) != NET_OK) fail("MegaWiFi module not found");
    sprintf(buf, "Module fw %u.%u %s", mod.major, mod.minor, mod.variant);
    say(buf);

    if (net_ensure_ap(devcfg_wifi_ssid, devcfg_wifi_pass) != NET_OK) fail("Saving AP config failed");
    say("Joining AP...");
    if (net_join(ip) != NET_OK) fail("Association / DHCP failed");
    sprintf(buf, "IP %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    say(buf);
    put_row(LIST_TOP - 1, PAL0, "CALLSIGN ALT  GS  DIST");

    static const char spin[] = "|/-\\";
    for (u16 n = 1;; n++) {
        poll_once(n);
        /* Waiting here lets the user task run mw_process(); the spinner shows
         * the main loop is alive between (blocking) polls. */
        for (u16 f = 0; f < POLL_FRAMES; f++) {
            buf[0] = spin[(f >> 3) & 3];
            buf[1] = 0;
            VDP_drawText(buf, COLS - 1, 0);
            SYS_doVBlankProcess();
        }
    }
    return 0;
}
