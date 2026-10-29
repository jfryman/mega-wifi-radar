#include "popup.h"
#include "feed.h"
#include "fmt.h"
#include "radar.h"
#include "settings.h"
#include "theme.h"
#include "tlist.h"
#include "traffic.h"

#define SETTLE_FRAMES   15      /* selection still this long before its detail is fetched */
#define LABEL_W         11      /* " LABEL......VALUE": labels <= 10 chars, values from col 12 to col 26 */

enum { D_NONE, D_LOADING, D_OK, D_MISSING };

static bool open;
static u8 d_state;
static struct ar_detail det;
static u8 det_icao[3];
static u16 settle;

/* ------------------------------------------------------------ formatting */

/* 11250 -> "11,250"; negative values keep their sign. */
static void thousands(char *buf, s32 v)
{
    char tmp[12];
    u16 n = 0, len;
    bool neg = v < 0;
    u32 u = neg ? -v : v;

    do { tmp[n++] = '0' + u % 10; u /= 10; } while (u);
    len = 0;
    if (neg) buf[len++] = '-';
    while (n) {
        buf[len++] = tmp[--n];
        if (n && !(n % 3)) buf[len++] = ',';
    }
    buf[len] = 0;
}

/* Copy a space-padded field and trim it. */
static void field(char *out, const char *in, u16 n)
{
    memcpy(out, in, n);
    out[n] = 0;
    while (n && out[n - 1] == ' ') out[--n] = 0;
}

static void row(u16 r, u16 pal, const char *label, const char *value)
{
    char buf[POPUP_COLS + 1];

    sprintf(buf, " %-*s%s", LABEL_W, label, value);
    radar_popup_row(r, buf, pal);
}

static const char *cat_name(u8 cat)
{
    static const char *const a[8] = { "", "LIGHT", "SMALL", "LARGE", "HIGH VORTEX", "HEAVY", "HIGH PERF", "ROTORCRAFT" };
    static const char *const b[8] = { "", "GLIDER", "LIGHTER THAN AIR", "PARACHUTIST", "ULTRALIGHT", "", "DRONE", "SPACE" };
    static const char *const c[8] = { "", "EMERG VEHICLE", "SERVICE VEHICLE", "OBSTACLE", "OBSTACLE", "OBSTACLE", "", "" };
    u8 hi = cat >> 4, lo = cat & 0x0F;

    if (lo > 7) return "";
    return hi == 0 ? a[lo] : hi == 1 ? b[lo] : hi == 2 ? c[lo] : "";
}

static const char *squawk_name(u16 sq)
{
    switch (sq) {
    case 0x7500: return "HIJACK";
    case 0x7600: return "RADIO FAILURE";
    case 0x7700: return "EMERGENCY";
    case 0x1200: return "VFR";
    }
    return "";
}

/* ------------------------------------------------------------ rows */

static void draw(void)
{
    s16 sel = radar_selected();
    char v[32], w[16], s[33];

    if (sel < 0) return;
    const struct ar_rec *r = &tracks[sel].next;
    bool have = d_state == D_OK && (det.dflags & AR_D_FOUND);
    bool emerg = r->flags & AR_F_EMERG;

    /* header: callsign, ICAO address on the right */
    field(s, r->cs, 8);
    sprintf(v, " %-18s%02X%02X%02X", s, r->icao[0], r->icao[1], r->icao[2]);
    radar_popup_row(1, v, emerg ? PAL_TEXT_EMERG : PAL2);
    radar_popup_row(2, " --------------------------", PAL1);

    if (have) {
        char reg[9], type[5];
        field(reg, det.reg, 8);
        field(type, det.type, 4);
        if (det.dflags & AR_D_YEAR) sprintf(v, " %-8s %-4s  BUILT %u", reg, type, det.year);
        else sprintf(v, " %-8s %s", reg, type);
        radar_popup_row(3, v, PAL0);
        field(s, det.desc, 26);
        sprintf(v, " %s", s);
        radar_popup_row(4, v, PAL0);
        field(s, det.owner, 26);
        sprintf(v, " %s", s);
        radar_popup_row(5, v, PAL1);
    } else {
        radar_popup_row(3, d_state == D_MISSING ? " NO DETAILS FROM THE RELAY" : " LOADING DETAILS...", PAL1);
        radar_popup_row(4, "", PAL0);
        radar_popup_row(5, "", PAL0);
    }

    /* live: from the frame */
    if (r->flags & AR_F_GROUND) strcpy(v, "ON GROUND");
    else if (r->flags & AR_F_ALT_UNK) strcpy(v, "--");
    else { thousands(w, (s32)r->alt * 25); sprintf(v, "%s FT", w); }
    row(7, PAL0, "ALTITUDE", v);

    if (r->flags & AR_F_VR_UNK) strcpy(v, "--");
    else if (r->vrate > -2 && r->vrate < 2) strcpy(v, "LEVEL");
    else { thousands(w, (s32)r->vrate * 64); sprintf(v, "%s%s FPM", r->vrate > 0 ? "+" : "", w); }
    row(8, PAL0, "VERT SPEED", v);

    if (have && (det.dflags & AR_D_GEOM_ALT)) { thousands(w, (s32)det.geom_alt * 25); sprintf(v, "%s FT", w); }
    else strcpy(v, "--");
    row(9, PAL0, "GPS ALT", v);
    if (have && (det.dflags & AR_D_SEL_ALT)) { thousands(w, (s32)det.sel_alt * 25); sprintf(v, "%s FT", w); }
    else strcpy(v, "--");
    row(10, PAL0, "SEL ALT", v);
    if (have && (det.dflags & AR_D_SEL_HDG)) sprintf(v, "%03u", det.sel_hdg);
    else strcpy(v, "--");
    row(11, PAL0, "SEL HDG", v);
    if (have && (det.dflags & AR_D_QNH)) {
        u16 in100 = ((u32)det.qnh * 2953 + 5000) / 10000;       /* 0.1 hPa -> 0.01 inHg */
        sprintf(v, "%u.%02u IN HG", in100 / 100, in100 % 100);
        row(12, PAL0, "ALTIMETER", v);
        sprintf(v, "%u.%u HPA", det.qnh / 10, det.qnh % 10);
        row(13, PAL0, "QNH", v);
    } else {
        row(12, PAL0, "ALTIMETER", "--");
        row(13, PAL0, "QNH", "--");
    }

    if (r->flags & AR_F_GS_UNK) strcpy(v, "--");
    else sprintf(v, "%u KT", r->gs * 2);
    row(15, PAL0, "SPEED", v);
    if (r->flags & AR_F_TRK_UNK) strcpy(v, "--");
    else sprintf(v, "%03u", ((u16)r->track * 45 + 16) >> 5);
    row(16, PAL0, "TRACK", v);
    if (r->squawk == AR_SQUAWK_NONE) strcpy(v, "--");
    else sprintf(v, "%04X %s", r->squawk, squawk_name(r->squawk));
    row(17, emerg ? PAL_TEXT_EMERG : PAL0, "SQUAWK", v);
    if (r->cat == AR_CAT_NONE) strcpy(v, "--");
    else sprintf(v, "%c%u %s", 'A' + (r->cat >> 4), r->cat & 0x0F, cat_name(r->cat));
    row(18, PAL0, "CATEGORY", v);

    s32 x = r->x, y = r->y;
    u16 d10 = ((u32)isqrt32((u32)(x * x) + (u32)(y * y)) * 10) >> 5;
    sprintf(v, "%u.%u NM", d10 / 10, d10 % 10);
    row(20, PAL0, "DISTANCE", v);
    sprintf(v, "%03u FROM %s", ((u16)tracks[sel].next_brg * 45 + 16) >> 5, settings.icao);
    row(21, PAL0, "BEARING", v);
    sprintf(v, "%s%s", (r->flags & AR_F_GROUND) ? "ON GROUND" : "AIRBORNE",
            (r->flags & AR_F_STALE) ? ", STALE" : "");
    row(22, (r->flags & AR_F_STALE) ? PAL1 : PAL0, "STATUS", v);
    if (have && (det.dflags & AR_D_SEEN)) sprintf(v, "%u S AGO", det.seen);
    else strcpy(v, "--");
    row(23, PAL0, "LAST SEEN", v);

    radar_popup_row(25, " UP/DOWN NEXT   C/B CLOSE", PAL1);
}

/* ------------------------------------------------------------ detail */

static void fetch(void)
{
    s16 sel = radar_selected();
    const struct ar_detail *d;

    settle = 0;
    if (sel < 0) return;
    const u8 *icao = tracks[sel].next.icao;
    if (feed_detail(icao, &d) == FEED_OK && d->icao[0] == icao[0] && d->icao[1] == icao[1] &&
            d->icao[2] == icao[2]) {
        det = *d;
        d_state = D_OK;
    } else if (d_state != D_OK || memcmp(det_icao, icao, 3)) {
        d_state = D_MISSING;        /* a failed refresh keeps the last good detail */
    }
    memcpy(det_icao, icao, 3);
    draw();
}

/* ------------------------------------------------------------ API */

void popup_open(void)
{
    if (radar_selected() < 0) tlist_move(0);
    if (radar_selected() < 0) return;           /* nothing to show */
    for (u16 r = 0; r < PANEL_ROWS; r++) radar_popup_row(r, "", PAL0);
    open = TRUE;
    d_state = D_LOADING;
    draw();
    radar_popup(TRUE);
    fetch();
}

void popup_close(void)
{
    open = FALSE;
    settle = 0;
    radar_popup(FALSE);
}

bool popup_is_open(void) { return open; }

void popup_selection_changed(void)
{
    if (!open) return;
    d_state = D_LOADING;
    settle = 1;
    draw();
}

void popup_after_poll(void)
{
    if (!open) return;
    if (radar_selected() < 0) {         /* the aircraft left the feed */
        popup_close();
        return;
    }
    if (settle) draw();                 /* still moving: popup_tick fetches */
    else fetch();
}

void popup_tick(void)
{
    if (open && settle && ++settle > SETTLE_FRAMES) fetch();
}
