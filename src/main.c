/* mega_wifi_radar: ATC radar scope on MegaWiFi Rev.C.
 * Boot (splash.c, a 787 departure narrated by the boot steps): detect module, join Wi-Fi. Then the radar renderer runs
 * in the V-Int (radar.c) while this loop polls the feed once per sweep and
 * handles the pad. After radar_start() this file must not touch the VDP. */
#include <genesis.h>
#include "devcfg.h"
#include "feed.h"
#include "fmt.h"
#include "geo.h"
#include "net.h"
#include "popup.h"
#include "radar.h"
#include "setup.h"
#include "splash.h"
#include "settings.h"
#include "theme.h"
#include "tlist.h"
#include "traffic.h"

#define HOLD_FRAMES     40      /* C held this long cycles the sort order */
#define REPEAT_DELAY    16      /* up/down held: frames before auto-repeat, then one row per REPEAT_RATE */
#define REPEAT_RATE     5
#define STALE_SECS      15      /* panel shows STALE after this without data */

#ifndef FAKE_FEED
static void wait_frames(u16 n)
{
    u32 t = vtimer;
    while (vtimer - t < n) {}
}

/* One frame from the relay before takeoff, so a relay from another release (lock-step: relay vX
 * serves cart vX) is reported at boot rather than as an error on the radar. Typed URLs too. */
static enum feed_err relay_probe(void)
{
    const struct ar_hdr *h;
    struct feed_stats st;
    enum feed_err e = feed_fetch(&h, &st);

    return e == FEED_ERR_VERSION ? e : FEED_OK;     /* other errors: the radar shows and retries them */
}

/* Detect the module, load saved settings, join Wi-Fi, narrated by the splash: each step is an
 * ATC call (lode/ui/boot-splash.md). Falls back to the setup screen when there's no working
 * network or no relay configured; the splash then resumes at the hold line. */
static void boot_network(void)
{
    struct net_module_info mod;
    char buf[80];                   /* app_version can be long (git describe -dirty); splash cuts lines */
    u8 ip[4];

    splash_start(SP_GATE);
    net_init();
    if (net_detect(&mod) != NET_OK) {
        splash_phase(SP_NO_MODULE, "MEGAWIFI MODULE NOT FOUND");
        while (TRUE) {}
    }
    bool dev = !settings_dev_suppressed();      /* a setup reset turns dev defaults off */
    if (dev) settings_dev_defaults();
    bool loaded = settings_load();
    sprintf(buf, "%s  FW %u.%u %s  %s", app_version, mod.major, mod.minor, mod.variant,
            !dev ? "DEV OFF" : loaded ? "SETTINGS LOADED" : "DEFAULTS");
    splash_status(buf);
    if (dev) net_ensure_ap(devcfg_wifi_ssid, devcfg_wifi_pass);
    bool setup = JOY_readJoypad(JOY_1) & BUTTON_START;      /* hold START at boot: setup */
    splash_phase(SP_TAXI, NULL);
    for (;;) {
        if (!setup) {
            splash_phase(SP_HOLD, NULL);
            bool joined = net_join(ip) == NET_OK;
            enum feed_err fe = FEED_ERR_CONFIG;
            if (joined) {
                if (!settings.relay[0]) splash_phase(SP_RELAY, NULL);   /* AUTO: mDNS */
                if ((fe = feed_find_relay()) == FEED_OK) fe = relay_probe();
                if (fe == FEED_OK) break;
            }
            if (fe == FEED_ERR_VERSION) sprintf(buf, "RELAY MISMATCH: RUN RELAY %s", app_version);
            splash_phase(SP_HOLD_POS, !joined ? "WI-FI FAILED: OPENING SETUP" :
                         fe == FEED_ERR_VERSION ? buf : "NO RELAY FOUND: OPENING SETUP");
            wait_frames(120);
        }
        splash_stop();
        setup_run(TRUE);
        setup = FALSE;
        splash_start(SP_HOLD);
    }
    sprintf(buf, "IP %u.%u.%u.%u  RELAY %s", ip[0], ip[1], ip[2], ip[3], feed_relay_host());
    splash_status(buf);

    splash_phase(SP_TAKEOFF, NULL);
    splash_finish();
}
#else
/* Emulator build: the same departure, with fixed delays standing in for the network. */
static void boot_network(void)
{
    splash_start(SP_GATE);
    splash_status("EMULATOR BUILD  RECORDED FEED");     /* no version: shots go in the manual */
    splash_phase(SP_TAXI, NULL);
    splash_phase(SP_HOLD, NULL);
    splash_phase(SP_TAKEOFF, NULL);
    splash_finish();
}
#endif

/* ------------------------------------------------------------ side panel */

enum {
    ROW_HOME = 1, ROW_RANGE, ROW_TIME, ROW_COUNT, ROW_NET,
    ROW_LIST,                               /* sort header, LIST_ROWS entries, "more" (tlist.c) */
    ROW_SEP = ROW_LIST + LIST_ROWS + 2, ROW_MODES, ROW_DATA1, ROW_DATA2,
};
_Static_assert(ROW_DATA2 == 26, "the data credit must sit on the last safe row");

static const char *source_name(u8 src)
{
    switch (src) {
    case AR_SRC_ADSBFI:  return "adsb.fi";
    case AR_SRC_ADSBLOL: return "adsb.lol";
    case AR_SRC_OPENSKY: return "OpenSky";
    }
    return "?";
}

/* "CLASSIC ALL": theme + tag mode */
static void panel_modes(void)
{
    static const char *const tags[TAGS_MODES] = { "ALL", "SEL", "OFF" };
    char buf[16];

    sprintf(buf, "%-7s %s", themes[settings.theme].name, tags[settings.tag_mode]);
    radar_panel(ROW_MODES, buf);
}

static void panel_static(void)
{
    char buf[16];

    radar_panel(ROW_HOME, settings.icao);
    sprintf(buf, "RNG %3uNM", settings_range_nm());
    radar_panel(ROW_RANGE, buf);
    radar_panel(ROW_SEP, "-----------");
    panel_modes();
    radar_panel(ROW_DATA1, "Data:");
    tlist_draw(ROW_LIST);
    tlist_rebuild();                        /* radar_init() emptied the track table */
}

/* ------------------------------------------------------------ input */

static bool want_setup;

/* Radar controls (lode/ui/screen-layout.md). X is a 6-button shortcut: everything it does is
 * also in setup, so a 3-button pad loses nothing. */
static void handle_input(void)
{
    static u16 prev, c_held, dir_held;
    u16 j = radar_joy(), pressed = j & ~prev;

    prev = j;
    if (popup_is_open() && (pressed & BUTTON_B)) {
        popup_close();
        pressed &= ~BUTTON_B;               /* B closes the pop-up; it doesn't also zoom out */
    }
    if ((pressed & (BUTTON_A | BUTTON_B)) && !popup_is_open()) {
        s16 r = settings.range_idx + ((pressed & BUTTON_A) ? -1 : 1);
        if (r >= 0 && r < RANGE_COUNT) {
            char buf[16];
            settings.range_idx = r;
            sprintf(buf, "RNG %3uNM", settings_range_nm());
            radar_panel(ROW_RANGE, buf);
            radar_rescale();
        }
    }
    /* C: a tap (released before HOLD_FRAMES) opens / closes the pop-up; a hold changes the sort */
    if (j & BUTTON_C) {
        if (++c_held == HOLD_FRAMES) tlist_sort_next();
    } else {
        if (c_held && c_held < HOLD_FRAMES) {
            if (popup_is_open()) popup_close();
            else popup_open();
        }
        c_held = 0;
    }
    /* up/down: list selection, auto-repeating while held */
    u16 ud = j & (BUTTON_UP | BUTTON_DOWN);
    if (ud && (ud & pressed || (++dir_held >= REPEAT_DELAY && !((dir_held - REPEAT_DELAY) % REPEAT_RATE)))) {
        tlist_move((ud & BUTTON_UP) ? -1 : 1);
        popup_selection_changed();
    }
    if (!ud) dir_held = 0;
    if (pressed & (BUTTON_LEFT | BUTTON_RIGHT)) {
        settings.tag_mode = (settings.tag_mode + ((pressed & BUTTON_RIGHT) ? 1 : TAGS_MODES - 1)) % TAGS_MODES;
        radar_retag();
        panel_modes();
    }
    if (pressed & BUTTON_X) {
        settings.theme = (settings.theme + 1) % THEME_COUNT;
        radar_retheme();
        panel_modes();
    }
    if (pressed & BUTTON_START) want_setup = TRUE;
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
        tlist_rebuild();
        popup_after_poll();
    } else {
        radar_panel(ROW_NET, feed_err_str(e));
        popup_after_poll();
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

static void radar_up(void)
{
    radar_init();
    panel_static();
    radar_panel(ROW_NET, have_snap ? "NET OK" : "WAIT DATA");
    radar_start();
}

int main(bool hard)
{
    (void)hard;
    settings_init();
    boot_network();

    radar_up();
    /* ---- from here on: no VDP access in this file (except around setup) ---- */

    u16 rot = radar_rotations();
    u32 frame = vtimer, sec = vtimer;
    poll();
    while (TRUE) {
        while (vtimer == frame) {}
        frame = vtimer;
        main_beat++;
        handle_input();
        popup_tick();
        if (want_setup) {
            want_setup = FALSE;
            popup_close();
            radar_stop();
            setup_run(FALSE);
#ifndef FAKE_FEED
            if (net_rejoin_needed()) {          /* a Wi-Fi scan left the AP and nothing reconnected */
                u8 ip[4];
                net_join(ip);
            }
#endif
            radar_up();
            poll();                 /* refill the (cleared) track table now */
            rot = radar_rotations();
        }
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
