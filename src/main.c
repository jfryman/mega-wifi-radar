/* Network spike: detect module -> join AP -> plain-HTTP GET -> show body.
 * Proves the Rev.C UART link, Wi-Fi config and the HTTP channel end to end. */
#include <genesis.h>
#include "devcfg.h"
#include "net.h"

#define COLS        40
#define BODY_TOP    12
#define BODY_ROWS   (27 - BODY_TOP)

static u16 line = 1;

static void say(const char *s)
{
    VDP_clearTextLine(line);
    VDP_drawText(s, 0, line++);
}

/* "36.1245" / "-86.6782" from 1e-4 degree fixed point, no floats. */
static void fmt_deg_e4(char *out, s32 v)
{
    u32 a = v < 0 ? -v : v;
    sprintf(out, "%s%lu.%04lu", v < 0 ? "-" : "", a / 10000, a % 10000);
}

static void fail(const char *s)
{
    VDP_setTextPalette(PAL1);
    say(s);
    VDP_setTextPalette(PAL0);
    while (TRUE) SYS_doVBlankProcess();
}

/* Shows the first screenful of the body as text, wrapping at COLS. */
struct body_view {
    u16 col, row;
};

static void show_chunk(const u8 *data, u16 len, void *ctx)
{
    struct body_view *v = ctx;
    char c[2] = { 0, 0 };

    for (u16 i = 0; i < len && v->row < BODY_ROWS; i++) {
        if (data[i] == '\r') continue;
        if (data[i] == '\n' || v->col == COLS) {
            v->col = 0;
            v->row++;
            if (data[i] == '\n') continue;
            if (v->row == BODY_ROWS) break;
        }
        c[0] = (data[i] >= 32 && data[i] < 127) ? data[i] : '.';
        VDP_drawText(c, v->col++, BODY_TOP + v->row);
    }
}

int main(bool hard)
{
    struct net_module_info mod;
    struct net_http_result res;
    struct body_view view = { 0, 0 };
    u8 ip[4];
    char buf[COLS + 1];

    (void)hard;
    line = 1;   /* soft reset keeps RAM: statics aren't re-initialised */
    PAL_setColor(PAL1 * 16 + 15, RGB24_TO_VDPCOLOR(0xFF4040));
    say("mega_wifi_radar net spike (Rev.C)");
    {
        char lat[12], lon[12];
        fmt_deg_e4(lat, devcfg_home_lat_e4);
        fmt_deg_e4(lon, devcfg_home_lon_e4);
        sprintf(buf, "Home %s %s %s %unm", devcfg_home_icao, lat, lon, devcfg_range_nm);
        say(buf);
    }

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

    sprintf(buf, "GET %.36s", devcfg_http_url);
    say(buf);
    enum net_err e = net_http_get(devcfg_http_url, show_chunk, &view, &res);
    if (res.chunked) sprintf(buf, "HTTP %d len chunked", res.status);
    else sprintf(buf, "HTTP %d len %lu", res.status, res.content_len);
    say(buf);
    if (e != NET_OK) fail(e == NET_ERR_RECV ? "Body receive failed" : "HTTP request failed");
    sprintf(buf, "%lu B in %u chunks, %u frames", res.received, res.chunks, res.frames);
    say(buf);

    /* Prove the main loop keeps running with the user task idling. */
    static const char spin[] = "|/-\\";
    for (u16 f = 0;; f++) {
        buf[0] = spin[(f >> 3) & 3];
        buf[1] = 0;
        VDP_drawText(buf, COLS - 1, 0);
        SYS_doVBlankProcess();
    }
    return 0;
}
