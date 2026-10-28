/* Network spike: detect module -> join AP -> plain-HTTP GET -> show body.
 * Proves the Rev.C UART link, Wi-Fi config and the HTTP channel end to end. */
#include <genesis.h>
#include "net.h"

/* Optional, git-ignored: copy src/dev_config.h.example to src/dev_config.h. */
#if __has_include("dev_config.h")
#include "dev_config.h"
#endif
#ifndef DEV_WIFI_SSID
#define DEV_WIFI_SSID NULL     /* use whatever the module already has saved */
#define DEV_WIFI_PASS NULL
#endif
#ifndef DEV_HTTP_URL
#define DEV_HTTP_URL "http://example.com/"
#endif

#define COLS        40
#define BODY_TOP    12
#define BODY_ROWS   (27 - BODY_TOP)

static u16 line = 1;

static void say(const char *s)
{
    VDP_clearTextLine(line);
    VDP_drawText(s, 0, line++);
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
    PAL_setColor(PAL1 * 16 + 15, RGB24_TO_VDPCOLOR(0xFF4040));
    say("mega_wifi_radar net spike (Rev.C)");

    net_init();
    if (net_detect(&mod) != NET_OK) fail("MegaWiFi module not found");
    sprintf(buf, "Module fw %u.%u %s", mod.major, mod.minor, mod.variant);
    say(buf);

    if (net_ensure_ap(DEV_WIFI_SSID, DEV_WIFI_PASS) != NET_OK) fail("Saving AP config failed");
    say("Joining AP...");
    if (net_join(ip) != NET_OK) fail("Association / DHCP failed");
    sprintf(buf, "IP %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    say(buf);

    say("GET " DEV_HTTP_URL);
    enum net_err e = net_http_get(DEV_HTTP_URL, show_chunk, &view, &res);
    sprintf(buf, "HTTP %d len %lu", res.status, res.content_len);
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
