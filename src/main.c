/* mega_wifi_radar: ATC radar scope on MegaWiFi Rev.C.
 * Boot (text screen): detect module, join Wi-Fi. Then the radar renderer runs
 * in the V-Int (radar.c) while this loop polls the feed once per sweep and
 * handles the pad. After radar_start() this file must not touch the VDP. */
#include <genesis.h>
#include "devcfg.h"
#include "feed.h"
#include "fmt.h"
#include "geo.h"
#include "net.h"
#include "radar.h"
#include "settings.h"
#include "theme.h"
#include "traffic.h"

#define HOLD_FRAMES     40      /* C held this long cycles the tag mode */
#define STALE_SECS      15      /* panel shows STALE after this without data */

static u16 line = 1;

static void say(const char *s)
{
    VDP_clearTextLine(line);
    VDP_drawText(s, 1, line++);
}

#ifndef FAKE_FEED
static void fail(const char *s)
{
    VDP_setTextPalette(PAL3);
    say(s);
    while (TRUE) SYS_doVBlankProcess();
}

static void boot_network(void)
{
    struct net_module_info mod;
    char buf[41];
    u8 ip[4];

    if (!devcfg_relay_url) fail("Set DEV_RELAY_URL in src/dev_config.h");
    say("Detecting MegaWiFi...");
    net_init();
    if (net_detect(&mod) != NET_OK) fail("MegaWiFi module not found");
    sprintf(buf, "Module fw %u.%u %s", mod.major, mod.minor, mod.variant);
    say(buf);
    if (net_ensure_ap(devcfg_wifi_ssid, devcfg_wifi_pass) != NET_OK) fail("Saving AP config failed");
    say("Joining Wi-Fi...");
    if (net_join(ip) != NET_OK) fail("Association / DHCP failed");
    sprintf(buf, "IP %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    say(buf);
}
#endif

/* ------------------------------------------------------------ side panel */

enum {
    ROW_HOME = 1, ROW_RANGE, ROW_TIME, ROW_COUNT, ROW_NET,
    ROW_SEP1, ROW_SEL, ROW_SEL_ALT, ROW_SEL_GS, ROW_SEL_TRK, ROW_SEL_SQK, ROW_SEL_POS,
    ROW_SEP2, ROW_HELP1 = 15, ROW_HELP2, ROW_HELP3, ROW_HELP4, ROW_HELP5,
    ROW_THEME = 21, ROW_TAGS, ROW_DATA1 = 25, ROW_DATA2,
};

static const char *source_name(u8 src)
{
    switch (src) {
    case AR_SRC_ADSBFI:  return "adsb.fi";
    case AR_SRC_ADSBLOL: return "adsb.lol";
    case AR_SRC_OPENSKY: return "OpenSky";
    }
    return "?";
}

static void panel_static(void)
{
    static const char *const tags[TAGS_MODES] = { "TAGS ALL", "TAGS SEL", "TAGS OFF" };
    char buf[16];

    radar_panel(ROW_HOME, devcfg_home_icao);
    sprintf(buf, "RNG %3uNM", settings_range_nm());
    radar_panel(ROW_RANGE, buf);
    radar_panel(ROW_SEP1, "-----------");
    radar_panel(ROW_SEP2, "-----------");
    radar_panel(ROW_HELP1, "A/B  ZOOM");
    radar_panel(ROW_HELP2, "C    SELECT");
    radar_panel(ROW_HELP3, "HOLD C TAGS");
    radar_panel(ROW_HELP4, "PAD  PAN");
    radar_panel(ROW_HELP5, "STRT THEME");
    radar_panel(ROW_THEME, themes[settings.theme].name);
    radar_panel(ROW_TAGS, tags[settings.tag_mode]);
    radar_panel(ROW_DATA1, "Data:");
}

static void panel_selection(void)
{
    s16 sel = radar_selected();
    char buf[16];

    if (sel < 0 || !tracks[sel].used || !tracks[sel].shown) {
        radar_panel(ROW_SEL, "NO SELECT");
        for (u16 r = ROW_SEL_ALT; r <= ROW_SEL_POS; r++) radar_panel(r, "");
        return;
    }
    struct ar_rec r = tracks[sel].cur;
    s32 x = r.x, y = r.y;
    u16 d10 = ((u32)isqrt32(x * x + y * y) * 10) >> 5;
    u16 brg = ((u16)geo_angle(r.x, r.y) * 45 + 16) >> 5;     /* steps -> degrees */

    memcpy(buf, r.cs, 8);
    buf[8] = 0;
    radar_panel(ROW_SEL, buf);
    if (r.flags & AR_F_GROUND) strcpy(buf, "ALT GND");
    else if (r.flags & AR_F_ALT_UNK) strcpy(buf, "ALT ---");
    else sprintf(buf, "ALT %03d %c", (r.alt < 0 ? 0 : r.alt) >> 2,
                 r.vrate > 2 ? '^' : r.vrate < -2 ? 'v' : ' ');
    radar_panel(ROW_SEL_ALT, buf);
    sprintf(buf, "GS  %3u", r.gs * 2);
    radar_panel(ROW_SEL_GS, buf);
    sprintf(buf, "TRK %03u", ((u16)r.track * 45 + 16) >> 5);
    radar_panel(ROW_SEL_TRK, buf);
    if (r.squawk == AR_SQUAWK_NONE) strcpy(buf, "SQK ----");
    else sprintf(buf, "SQK %04X", r.squawk);
    radar_panel(ROW_SEL_SQK, buf);
    sprintf(buf, "%u.%uNM %03u", d10 / 10, d10 % 10, brg);
    radar_panel(ROW_SEL_POS, buf);
}

/* ------------------------------------------------------------ input */

static u16 dist_of(s16 i)
{
    s32 x = tracks[i].cur.x, y = tracks[i].cur.y;
    return isqrt32(x * x + y * y);
}

/* Next shown aircraft by distance after the current selection (wraps). */
static void select_next(void)
{
    s16 cur = radar_selected(), best = -1, first = -1;
    u16 cd = cur >= 0 && tracks[cur].used ? dist_of(cur) : 0;
    u16 bd = 0xFFFF, fd = 0xFFFF;

    for (s16 i = 0; i < TR_MAX; i++) {
        if (!tracks[i].used || !tracks[i].shown || tracks[i].drop) continue;
        u16 d = dist_of(i);
        if (d < fd) { fd = d; first = i; }
        if ((cur < 0 || d > cd || (d == cd && i > cur)) && d < bd) { bd = d; best = i; }
    }
    radar_select(best >= 0 ? best : first);
    if (settings.tag_mode == TAGS_SELECTED) radar_retag();
    panel_selection();
}

static void handle_input(void)
{
    static u16 prev;
    static u16 c_held;
    u16 j = radar_joy(), pressed = j & ~prev;

    prev = j;
    if (pressed & (BUTTON_A | BUTTON_B)) {
        s16 r = settings.range_idx + ((pressed & BUTTON_A) ? -1 : 1);
        if (r >= 0 && r < RANGE_COUNT) {
            char buf[16];
            settings.range_idx = r;
            sprintf(buf, "RNG %3uNM", settings_range_nm());
            radar_panel(ROW_RANGE, buf);
            radar_rescale();
        }
    }
    if (j & BUTTON_C) {
        if (++c_held == HOLD_FRAMES) {
            settings.tag_mode = (settings.tag_mode + 1) % TAGS_MODES;
            radar_retag();
            panel_static();
        }
    } else {
        if (c_held && c_held < HOLD_FRAMES) select_next();
        c_held = 0;
    }
    if (pressed & BUTTON_START) {
        settings.theme = (settings.theme + 1) % THEME_COUNT;
        radar_retheme();
        panel_static();
    }
    s16 dx = (j & BUTTON_RIGHT) ? 2 : (j & BUTTON_LEFT) ? -2 : 0;
    s16 dy = (j & BUTTON_DOWN) ? 2 : (j & BUTTON_UP) ? -2 : 0;
    if (dx || dy) radar_pan(dx, dy);
}

/* ------------------------------------------------------------ poll */

static u32 snap_time, snap_vtimer, last_ok_vtimer;
static bool have_snap;

static void poll(void)
{
    const struct ar_hdr *h;
    struct feed_stats st;
    char buf[16];
    enum feed_err e = feed_fetch(&h, &st);

    if (e == FEED_OK) {
        traffic_ingest(h);
        snap_time = h->time;
        snap_vtimer = last_ok_vtimer = vtimer;
        have_snap = TRUE;
        sprintf(buf, "%2u AC", traffic_count());
        radar_panel(ROW_COUNT, buf);
        radar_panel(ROW_NET, (h->hflags & AR_H_UPSTREAM_ERR) ? "UPSTRM ERR" : "NET OK");
        radar_panel(ROW_DATA2, source_name(h->source));
        panel_selection();
    } else {
        radar_panel(ROW_NET, feed_err_str(e));
    }
}

static void clock_tick(void)
{
    char buf[16];

    if (!have_snap) return;
    u32 now = snap_time + (vtimer - snap_vtimer) / 60;
    u32 sod = now % 86400UL;
    sprintf(buf, "%02lu:%02lu:%02luZ", sod / 3600, (sod / 60) % 60, sod % 60);
    radar_panel(ROW_TIME, buf);
#ifdef RADAR_DEBUG
    sprintf(buf, "L%u A%lu", late_frames, vint_lines_sum / 60);
    late_frames = main_beat = vint_lines_max = 0;
    vint_lines_sum = 0;
    radar_panel(23, buf);
    for (u16 i = 0; i < 8; i += 2) {
        sprintf(buf, "%u:%lu %u:%lu", i, prof_sum[i] / 60, i + 1, prof_sum[i + 1] / 60);
        prof_sum[i] = prof_sum[i + 1] = 0;
        radar_panel(15 + i / 2, buf);
    }
#endif
    u32 age = (vtimer - last_ok_vtimer) / 60;
    if (age > STALE_SECS) {
        sprintf(buf, "STALE %lus", age);
        radar_panel(ROW_NET, buf);
    }
}

int main(bool hard)
{
    (void)hard;
    line = 1;   /* soft reset keeps RAM: statics aren't re-initialised */
    settings_init();
    VDP_drawText("mega_wifi_radar", 1, line++);
#ifndef FAKE_FEED
    boot_network();
#else
    say("FAKE FEED (emulator build)");
#endif

    radar_init();
    panel_static();
    panel_selection();
    radar_panel(ROW_NET, "WAIT DATA");
    radar_start();
    /* ---- from here on: no VDP access in this file ---- */

    u16 rot = radar_rotations();
    u32 frame = vtimer, sec = vtimer;
    poll();
    while (TRUE) {
        while (vtimer == frame) {}
        frame = vtimer;
        main_beat++;
        handle_input();
        if (radar_rotations() != rot) {
            rot = radar_rotations();
            poll();
        }
        if (vtimer - sec >= 60) {
            sec = vtimer;
            clock_tick();
        }
    }
    return 0;
}
