#include "setup.h"
#include "feed.h"
#include "airports.h"
#include "chime.h"
#include "keyboard.h"
#include "net.h"
#include "radar.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"

enum { R_THEME, R_RANGE, R_TAGS, R_WX, R_MAP, R_SOUND, R_AMBIENT, R_AIRPORT, R_WIFI, R_RELAY, R_GUIDE, R_CONTROLS, R_RESET,
       R_SAVE, R_EXIT, R_COUNT };
static const char *const labels[R_COUNT] = {
    "THEME", "RANGE", "TAGS", "WEATHER", "MAP", "SOUND", "AMBIENT", "AIRPORT", "WI-FI", "RELAY", "GUIDE", "CONTROLS", "RESET",
    "SAVE & EXIT", "EXIT",
};
static const char *const ambient_names[AMBIENT_MODES] = { "OFF", "AFTER 1 MIN", "AFTER 5 MIN", "AFTER 15 MIN" };
static const char *const tag_names[TAGS_MODES] = { "ALL", "SELECTED", "OFF" };
static const char *const sound_names[SOUND_MODES] = { "ON", "OFF" };
static const char *const wx_names[WX_MODES] = { "ON", "OFF" };
static const char *const map_names[MAP_MODES] = { "ON", "OFF" };

/* Settings in blocks: the scope (THEME ... MAP), sound + ambient, the airport (its name takes a line), the network;
 * then the five actions; EXIT on row 24. */
static const u8 row_y[R_COUNT] = { 3, 4, 5, 6, 7, 9, 10, 12, 15, 16, 20, 21, 22, 23, 24 };
#define ROW_Y(r)    row_y[r]
#define HINT_ROW    26                                  /* below ui_status's row 25 */
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
    case R_WX: ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, wx_names[settings.wx]); break;
    case R_MAP: ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, map_names[settings.map]); break;
    case R_SOUND: ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, sound_names[settings.sound]); break;
    case R_AMBIENT: ui_field(PAL2, VAL_X, ROW_Y(r), VAL_W, ambient_names[settings.ambient]); break;
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
    ui_text(PAL1, 1, HINT_ROW, "L/R CHANGE  A EDIT  C JUMP  START SAVE");
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
        { "C THEN A", "COCKPIT VIEW" },
        { "B", "CLOSE A FLYOVER BANNER" },
        { "HOLD C", "CHANGE THE SORT ORDER" },
        { "LEFT/RIGHT", "TAGS: ALL / SEL / OFF" },
        { "X", "NEXT THEME (6-BUTTON)" },
        { "Y", "COCKPIT VIEW (6-BUTTON)" },
        { "Z", "WEATHER ON/OFF (6-BUTTON)" },
        { "START", "SETUP" },
        { "COCKPIT", NULL },
        { "B", "BACK TO THE RADAR" },
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

/* ------------------------------------------------------------ guide */

/* GUIDE: what's on the radar, a page at a time (lode/ui/guide.md). Text from col 2, descriptions from col 15
 * (<= 24 chars: col 38 is the last safe one). A row is { pal, left, right }; pal 0xFF: a sample tile in the
 * left column (right: the tile, '0' + index into the samples), then its text. Colours are the theme's own. */
#define G_TILE  0xFF
struct grow { u8 pal; const char *l, *r; };
static const struct grow g_tags[] = {
    { PAL1, "SWA2011", "CALLSIGN" },
    { PAL1, "006 156", "600 FT, 156 KNOTS" },
    { PAL0, "", "ALTITUDE IN HUNDREDS OF" },
    { PAL0, "", "FEET, THEN SPEED IN KT" },
    { PAL0, NULL, NULL },
    { PAL1, "LOW", "BELOW 10,000 FT, GROUND" },
    { PAL2, "MID", "10,000 TO 17,999 FT" },
    { PAL0, "HIGH", "18,000 FT AND ABOVE" },
    { PAL3, "EMERGENCY", "SQUAWK 7500/7600/7700" },
    { PAL0, NULL, NULL },
    { PAL0, "BRIGHT BLIP", "FRESH: JUST SWEPT" },
    { PAL0, "DOTS BEHIND", "LAST 5 POSITIONS: 20 S" },
    { PAL0, "HOLLOW BLIP", "NO REPORT FOR 15 S" },
    { PAL0, NULL, NULL },
    { PAL1, "STARS, ERAM", "THE TAG SITS AT THE END" },
    { PAL1, "", "OF A SHORT LEADER LINE" },
};
static const struct grow g_wx[] = {
    { PAL1, "", "RAIN FROM NEXRAD WEATHER" },
    { PAL1, "", "RADAR, UNDER THE TRAFFIC" },
    { PAL0, NULL, NULL },
    { G_TILE, "LIGHT", "0  20-35 DBZ: RAIN" },
    { G_TILE, "MODERATE", "1  35-50 DBZ: HEAVY RAIN" },
    { G_TILE, "HEAVY", "2  50+ DBZ: STORMS, HAIL" },
    { PAL0, NULL, NULL },
    { PAL0, "", "NEW EVERY 5 MINUTES AND" },
    { PAL0, "", "AFTER EVERY ZOOM. A TAG" },
    { PAL0, "", "HIDES THE RAIN UNDER IT." },
    { PAL0, NULL, NULL },
    { PAL1, "US ONLY", "ELSEWHERE: A CLEAR SCOPE" },
    { PAL1, "TURN OFF", "WEATHER IN SETUP, OR Z" },
};
static const struct grow g_map[] = {
    { G_TILE, "RUNWAY", "3  SOLID BAR, TO SCALE" },
    { G_TILE, "APPROACH", "4  DASHES: THE RUNWAY'S" },
    { PAL0, "", "CENTRELINE, OUT 10 NM" },
    { PAL0, "TICKS", "ONE EVERY MILE (EVERY" },
    { PAL0, "", "5 NM ON THE 25 NM SCOPE)" },
    { PAL0, NULL, NULL },
    { PAL2, "", "AIRLINERS DESCEND ABOUT" },
    { PAL2, "", "300 FT A MILE ON FINAL:" },
    { PAL0, "10 NM OUT", "ABOUT 3,000 FT (TAG 030)" },
    { PAL0, "5 NM OUT", "ABOUT 1,600 FT (TAG 016)" },
    { PAL0, "1 NM OUT", "ABOUT 300 FT" },
    { PAL0, NULL, NULL },
    { PAL1, "", "A TAG ON THE DASHES, ITS" },
    { PAL1, "", "ALTITUDE COUNTING DOWN:" },
    { PAL1, "", "LANDING ON THAT RUNWAY." },
    { PAL1, "", "(YOUR AIRPORT, TO 25 NM)" },
};
static const struct grow g_panel[] = {
    { PAL0, "KBNA", "YOUR HOME AIRPORT" },
    { PAL0, "RNG  25NM", "RANGE: THE OUTER RING" },
    { PAL0, "17:16:00Z", "TIME, UTC" },
    { PAL0, "28 AC", "AIRCRAFT ON THE SCOPE" },
    { PAL0, "NET OK", "LIVE (AUTO: TOURING)" },
    { PAL0, "STALE 20S", "NO NEW DATA FOR 20 S" },
    { PAL0, "UPSTRM ERR", "THE RELAY'S FEED FAILED" },
    { PAL0, "RELAY VER?", "RELAY: ANOTHER RELEASE" },
    { PAL0, NULL, NULL },
    { PAL0, "- NEAREST -", "LIST ORDER: HOLD C" },
    { PAL0, "v13 MORE", "MORE BELOW: PRESS DOWN" },
    { PAL0, "CLASSIC ALL", "THEME; TAGS (LEFT/RIGHT)" },
    { PAL0, "adsb.fi IEM", "DATA: TRAFFIC, WEATHER" },
};
static const struct { const char *title; const struct grow *rows; u16 n; } g_pages[] = {
    { "TAGS AND BLIPS", g_tags, sizeof(g_tags) / sizeof(g_tags[0]) },
    { "WEATHER", g_wx, sizeof(g_wx) / sizeof(g_wx[0]) },
    { "VIDEO MAP", g_map, sizeof(g_map) / sizeof(g_map[0]) },
    { "SIDE PANEL", g_panel, sizeof(g_panel) / sizeof(g_panel[0]) },
};
#define G_PAGES (sizeof(g_pages) / sizeof(g_pages[0]))

/* Sample tiles: the weather stipple (C_WX), a runway bar and a dashed centreline (C_MAP), 3 cells wide each. */
#define MAP_BAR     0x00000000UL, 0x00000000UL, 0x00000000UL, 0xBBBBBBBBUL, 0xBBBBBBBBUL, 0, 0, 0
static const u32 g_map_tiles[2][8] = {
    { MAP_BAR },
    { 0, 0, 0, 0xBBB000BBUL, 0, 0, 0, 0 },
};
#undef MAP_BAR

static void guide_draw(u16 page)
{
    char buf[40];

    ui_clear();
    VDP_loadTileData((const u32 *)radar_wx_tiles, TILE_USER_INDEX, 3, CPU);
    VDP_loadTileData((const u32 *)g_map_tiles, TILE_USER_INDEX + 3, 2, CPU);
    sprintf(buf, "GUIDE %u/%u: %s", page + 1, (u16)G_PAGES, g_pages[page].title);
    ui_text(PAL0, 2, 1, buf);
    for (u16 i = 0, y = 4; i < g_pages[page].n; i++, y++) {
        const struct grow *r = &g_pages[page].rows[i];
        if (!r->l) continue;
        if (r->pal == G_TILE) {
            u16 t = TILE_USER_INDEX + r->r[0] - '0';
            for (u16 x = 0; x < 3; x++)
                VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, t), 2 + x, y);
            ui_text(PAL0, 6, y, r->l);
            ui_text(PAL1, 15, y, r->r + 3);
            continue;
        }
        ui_text(r->pal, 2, y, r->l);
        ui_text(r->pal == PAL0 ? PAL1 : r->pal, 15, y, r->r);
    }
    ui_status(PAL1, "LEFT/RIGHT: PAGE   B OR START: BACK");
}

static void guide_page(void)
{
    u16 page = 0;

    guide_draw(page);
    ui_wait_release();
    for (;;) {
        u16 p = ui_pad();
        if (p & (BUTTON_B | BUTTON_START)) break;
        if (p & (BUTTON_RIGHT | BUTTON_A | BUTTON_LEFT)) {
            page = (p & BUTTON_LEFT) ? (page + G_PAGES - 1) % G_PAGES : (page + 1) % G_PAGES;
            guide_draw(page);
        }
    }
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
    case R_WX: settings.wx = (settings.wx + WX_MODES + d) % WX_MODES; break;
    case R_MAP: settings.map = (settings.map + MAP_MODES + d) % MAP_MODES; break;
    case R_SOUND:
        settings.sound = (settings.sound + SOUND_MODES + d) % SOUND_MODES;
        chime_play();                       /* a preview, when it's on: the home airport's chime */
        break;
    case R_AMBIENT: settings.ambient = (settings.ambient + AMBIENT_MODES + d) % AMBIENT_MODES; break;
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
            case R_GUIDE:
                guide_page();
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
