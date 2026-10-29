#include "setup.h"
#include "feed.h"
#include "airports.h"
#include "keyboard.h"
#include "net.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"

enum { R_THEME, R_RANGE, R_TAGS, R_AIRPORT, R_WIFI, R_RELAY, R_CONTROLS, R_RESET, R_SAVE, R_EXIT, R_COUNT };
static const char *const labels[R_COUNT] = {
    "THEME", "RANGE", "TAGS", "AIRPORT", "WI-FI", "RELAY", "CONTROLS", "RESET", "SAVE & EXIT", "EXIT",
};
static const char *const tag_names[TAGS_MODES] = { "ALL", "SELECTED", "OFF" };

#define ROW_Y(r)    (3 + (r) * 2 + ((r) > R_AIRPORT))   /* airport name gets a line; EXIT ends on row 22 */
#define VAL_X       14
#define VAL_W       25

static char wifi_ssid[NET_SSID_LEN + 1] __attribute__((unused));

static s16 airport_idx(void)
{
    return airport_find(settings.icao);
}

static void set_airport(s16 i)
{
    memcpy(settings.icao, airports[i].icao, 5);
    settings.lat_e4 = airports[i].lat_e4;
    settings.lon_e4 = airports[i].lon_e4;
}

static void draw_value(u16 r)
{
    char buf[41];
    s16 a;

    switch (r) {
    case R_THEME: ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, themes[settings.theme].name); break;
    case R_RANGE:
        sprintf(buf, "%u NM", settings_range_nm());
        ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, buf);
        break;
    case R_TAGS: ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, tag_names[settings.tag_mode]); break;
    case R_AIRPORT:
        a = airport_idx();
        ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, settings.icao);
        ui_field(PAL1, VAL_X, ROW_Y(r) + 1, VAL_W, a >= 0 ? airports[a].name : "(custom position)");
        break;
    case R_WIFI:
#ifdef FAKE_FEED
        ui_field(PAL1, VAL_X, ROW_Y(r), VAL_W, "(emulator: none)");
#else
        ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, wifi_ssid[0] ? wifi_ssid : "NOT SET");
#endif
        break;
    case R_RELAY:
        if (settings.relay[0]) {
            ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, settings.relay);
        } else {                                /* AUTO: show what mDNS found, if anything */
            const char *u = feed_relay_host();
            char buf[RELAY_LEN + 8];              /* ui_field cuts it to VAL_W */
            if (u[0]) sprintf(buf, "AUTO %s", u);
            else strcpy(buf, "AUTO (MDNS)");
            ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, buf);
        }
        break;
    }
}

static void draw_all(u16 cur)
{
    ui_clear();
    ui_text(PAL0, 2, 1, "MEGA WIFI RADAR - SETUP");
    for (u16 r = 0; r < R_COUNT; r++) {
        ui_text(r == cur ? PAL3 : PAL0, 2, ROW_Y(r), r == cur ? ">" : " ");
        ui_text(r == cur ? PAL3 : PAL0, 4, ROW_Y(r), labels[r]);
        draw_value(r);
    }
    ui_text(PAL1, 2, 23, "UP/DN ROW  L/R CHANGE  A EDIT");
    ui_text(PAL1, 2, 24, "C NEXT LETTER / AUTO  START SAVE");
}

static void cursor(u16 old, u16 cur)
{
    ui_text(PAL0, 2, ROW_Y(old), " ");
    ui_text(PAL0, 4, ROW_Y(old), labels[old]);
    ui_text(PAL3, 2, ROW_Y(cur), ">");
    ui_text(PAL3, 4, ROW_Y(cur), labels[cur]);
}

/* ------------------------------------------------------------ controls page */

/* Read-only: the radar and setup controls (the manual's controls page, on screen).
 * Descriptions start at col 14 and stay within 25 chars (col 38 is the last safe one). */
static void controls_page(void)
{
    static const char *const rows[][2] = {
        { "RADAR", NULL },
        { "A / B", "ZOOM IN / OUT" },
        { "UP / DOWN", "SELECT IN THE LIST" },
        { "C", "DETAILS (C OR B CLOSES)" },
        { "HOLD C", "CHANGE THE SORT ORDER" },
        { "LEFT/RIGHT", "TAGS: ALL / SEL / OFF" },
        { "X", "NEXT THEME (6-BUTTON)" },
        { "START", "SETUP" },
        { "SETUP", NULL },
        { "UP / DOWN", "CHOOSE A ROW" },
        { "LEFT/RIGHT", "CHANGE THE VALUE" },
        { "A", "EDIT / OPEN" },
        { "C", "NEXT LETTER / RELAY AUTO" },
        { "START", "SAVE & EXIT" },
    };
    u16 y = 4;

    ui_clear();
    ui_text(PAL0, 2, 1, "CONTROLS");
    for (u16 i = 0; i < sizeof(rows) / sizeof(rows[0]); i++, y++) {
        if (!rows[i][1]) {
            if (i) y++;
            ui_text(PAL2, 2, y, rows[i][0]);
            continue;
        }
        ui_text(PAL0, 3, y, rows[i][0]);
        ui_text(PAL1, 14, y, rows[i][1]);
    }
    ui_status(PAL1, "B OR START: BACK");
    ui_wait_release();
    while (!(ui_pad() & (BUTTON_B | BUTTON_START | BUTTON_A))) {}
    ui_wait_release();
}

/* ------------------------------------------------------------ Wi-Fi page */

/* RESET: pick what to forget, confirm with a second A, then hard-reset into the new flow. */
static void reset_page(void)
{
    static const char *const items[] = { "WI-FI NETWORKS", "SETTINGS", "EVERYTHING", "CANCEL" };
    static const char *const notes[] = {
        "FORGETS SAVED WI-FI, KEEPS SETTINGS.",  /* <= 36: cols 2..37 */
        "SETTINGS BACK TO DEFAULTS.",
        "WI-FI AND SETTINGS: AS NEW.",
        "",
    };
    u16 cur = 0;
    bool armed = FALSE;

    ui_clear();
    ui_text(PAL0, 2, 2, "RESET");
    ui_text(PAL1, 2, 18, "DEV BUILDS: DEV WI-FI/RELAY STAY OFF");
    ui_text(PAL1, 2, 19, "UNTIL ANOTHER ROM IS FLASHED.");
    ui_wait_release();
    for (;;) {
        for (u16 i = 0; i < 4; i++) {
            ui_text(i == cur ? PAL3 : PAL0, 2, 5 + i * 2, i == cur ? ">" : " ");
            ui_field(i == cur ? PAL3 : PAL0, 4, 5 + i * 2, 20, items[i]);
        }
        ui_field(PAL2, 2, 14, 36, notes[cur]);
        ui_status(armed ? PAL3 : PAL1, armed ? "PRESS A AGAIN TO RESET, B TO CANCEL" : "A SELECT  B BACK");
        u16 p = ui_pad();
        if (p & (BUTTON_UP | BUTTON_DOWN)) {
            if (p & BUTTON_UP && cur) cur--;
            if (p & BUTTON_DOWN && cur < 3) cur++;
            armed = FALSE;
        }
        if (p & BUTTON_B) { if (armed) { armed = FALSE; continue; } return; }
        if (!(p & BUTTON_A)) continue;
        if (cur == 3) return;
        if (!armed) { armed = TRUE; continue; }
#ifdef FAKE_FEED
        ui_status(PAL3, "NOTHING TO RESET IN AN EMULATOR");
        for (u16 f = 0; f < 60; f++) ui_pad();
        return;
#else
        ui_status(PAL2, "RESETTING...");
        bool ok = settings_reset(cur == 0 ? RESET_WIFI : cur == 1 ? RESET_SETTINGS : RESET_ALL);
        ui_status(ok ? PAL1 : PAL3, ok ? "DONE. RESTARTING..." : "RESET FAILED");
        for (u16 f = 0; f < 60; f++) ui_pad();
        if (ok) SYS_hardReset();
        armed = FALSE;
#endif
    }
}

#ifndef FAKE_FEED
#define MAX_APS 14

static void wifi_page(void)
{
    static struct net_ap aps[MAX_APS];
    char pass[65], buf[41];
    u16 cur = 0;
    s16 n;
    u8 ip[4];

    ui_clear();
    ui_text(PAL0, 2, 2, "WI-FI NETWORKS");
    ui_status(PAL2, "SCANNING...");
    n = net_scan(aps, MAX_APS);
    if (n < 0) n = 0;
    /* last entry: type an SSID (hidden networks) */
    for (;;) {
        for (s16 i = 0; i <= n; i++) {
            if (i < n) {
                /* "> SSID....................  -61 OPEN" (no %-Ns in SGDK's sprintf) */
                u16 k = 0;
                buf[k++] = i == cur ? '>' : ' ';
                buf[k++] = ' ';
                for (u16 c = 0; c < 24; c++) buf[k++] = aps[i].ssid[c] && c < strlen(aps[i].ssid) ? aps[i].ssid[c] : ' ';
                sprintf(buf + k, " %d%s", aps[i].rssi, aps[i].open ? " OPEN" : "");
            } else {
                sprintf(buf, "%c (OTHER / HIDDEN)", i == cur ? '>' : ' ');
            }
            ui_field(i == cur ? PAL3 : PAL0, 1, 4 + i, 38, buf);
        }
        ui_status(PAL1, n ? "A SELECT  B BACK" : "NO NETWORKS FOUND  A OTHER  B BACK");
        u16 p = ui_pad();
        if (p & BUTTON_UP && cur) cur--;
        if (p & BUTTON_DOWN && cur < n) cur++;
        if (p & BUTTON_B) return;
        if (!(p & BUTTON_A)) continue;

        char ssid[NET_SSID_LEN + 1];
        if (cur < n) {
            strcpy(ssid, aps[cur].ssid);
        } else {
            ssid[0] = 0;
            if (!kb_edit("NETWORK NAME (SSID)", ssid, NET_SSID_LEN) || !ssid[0]) return;
        }
        pass[0] = 0;
        if (cur >= n || !aps[cur].open) {
            sprintf(buf, "PASSWORD: %s", ssid);
            if (!kb_edit(buf, pass, 64)) return;
        }
        ui_clear();
        ui_text(PAL0, 2, 2, "WI-FI");
        ui_field(PAL2, 2, 4, 36, ssid);
        ui_status(PAL2, "CONNECTING...");
        enum net_err e = net_connect(ssid, pass, ip);
        if (e == NET_OK) {
            sprintf(buf, "SAVED + CONNECTED  IP %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            ui_status(PAL1, buf);
            strcpy(wifi_ssid, ssid);
        } else {
            static const char *const steps[] = { "", "AP CFG", "IP CFG", "DEFAULT AP", "FLASH SAVE", "READ-BACK" };
            if (e == NET_ERR_CFG) sprintf(buf, "SAVE FAILED: %s (B BACK)", steps[net_save_step]);
            else strcpy(buf, "SAVED; JOIN FAILED - PASSWORD? (B)");
            ui_status(PAL3, buf);
            while (!(ui_pad() & (BUTTON_B | BUTTON_A | BUTTON_START))) {}
            return;
        }
        for (u16 f = 0; f < 90; f++) ui_pad();
        return;
    }
}
#endif

/* ------------------------------------------------------------ main page */

static void change(u16 r, s16 d, u16 pressed)
{
    s16 a;

    switch (r) {
    case R_THEME: settings.theme = (settings.theme + THEME_COUNT + d) % THEME_COUNT; break;
    case R_RANGE: settings.range_idx = (settings.range_idx + RANGE_COUNT + d) % RANGE_COUNT; break;
    case R_TAGS: settings.tag_mode = (settings.tag_mode + TAGS_MODES + d) % TAGS_MODES; break;
    case R_AIRPORT:
        a = airport_idx();
        if (pressed & BUTTON_C) {
            /* jump to the next airport whose ident starts with another letter */
            char c0 = a >= 0 ? airports[a].icao[0] : 'A';
            s16 i = a < 0 ? 0 : a;
            while (++i < AIRPORT_COUNT && airports[i].icao[0] == c0) {}
            a = i >= AIRPORT_COUNT ? 0 : i;
        } else {
            a = a < 0 ? 0 : (a + AIRPORT_COUNT + d) % AIRPORT_COUNT;
        }
        set_airport(a);
        break;
    }
    draw_value(r);
}

bool setup_run(bool wifi_first)
{
    struct settings saved = settings;
    u16 cur = wifi_first ? R_WIFI : R_THEME;

#ifndef FAKE_FEED
    net_current_ssid(wifi_ssid);
    if (wifi_first) {
        wifi_page();
        if (!settings.relay[0] && !feed_relay_url()[0]) cur = R_RELAY;  /* AUTO found nothing */
    }
#endif
    draw_all(cur);
    ui_wait_release();
    for (;;) {
        u16 p = ui_pad(), old = cur;

        if (p & BUTTON_UP) cur = cur ? cur - 1 : R_COUNT - 1;
        if (p & BUTTON_DOWN) cur = (cur + 1) % R_COUNT;
        if (cur != old) cursor(old, cur);
        if (p & BUTTON_LEFT) change(cur, -1, p);
        if (p & BUTTON_RIGHT) change(cur, 1, p);
        if (p & BUTTON_C && cur == R_AIRPORT) change(cur, 0, p);
        if (p & BUTTON_C && cur == R_RELAY) { settings.relay[0] = 0; draw_value(R_RELAY); }  /* back to AUTO */

        bool save = p & BUTTON_START;
        if (p & BUTTON_A) {
            char buf[RELAY_LEN + 1];
            switch (cur) {
            case R_AIRPORT:
                strcpy(buf, settings.icao);
                if (kb_edit("AIRPORT ICAO CODE (E.G. KBNA)", buf, 4)) {
                    for (u16 i = 0; buf[i]; i++) if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
                    s16 a = airport_find(buf);
                    if (a >= 0) set_airport(a);
                }
                draw_all(cur);
                break;
#ifndef FAKE_FEED
            case R_WIFI:
                wifi_page();
                draw_all(cur);
                break;
#endif
            case R_RELAY:
                strcpy(buf, settings.relay);
                kb_edit("RELAY URL (EMPTY = AUTO)", buf, RELAY_LEN);
                strcpy(settings.relay, buf);
                draw_all(cur);
                break;
            case R_CONTROLS:
                controls_page();
                draw_all(cur);
                break;
            case R_RESET:
                reset_page();
                draw_all(cur);
                break;
            case R_SAVE:
                save = TRUE;
                break;
            case R_EXIT:
                settings = saved;
                return FALSE;
            }
        }
        if (save) {
#ifdef FAKE_FEED
            return FALSE;                       /* nothing to save to in an emulator */
#else
            ui_status(PAL2, "SAVING...");
            bool ok = settings_save();
            ui_status(ok ? PAL1 : PAL3, ok ? "SAVED" : "SAVE FAILED (SETTINGS KEPT)");
            for (u16 f = 0; f < 45; f++) ui_pad();
            return ok;
#endif
        }
    }
}
