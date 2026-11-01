/* Cockpit mode on the 32X (src/cockpit.h; lode/ui/cockpit.md for the design, lode/mars/renderer.md for the
 * split). The slave SH-2 runs this: the terrain patch, a track every TRACK_FRAMES, strips as the aircraft moves,
 * the HUD text, B to leave. The master SH-2 flies the camera between the fixes it posts and draws the view
 * (sh/rvox.c), so the view never stops while a request blocks here. Same rules and constants as src/cockpit.c. */
#include "cockpit.h"
#include "feed.h"
#include "fmt.h"
#include "shared.h"
#include "rvox.h"

#define TRACK_FRAMES    120                 /* a track every 2 s */
#define LOST_SECS       20                  /* no good fix this long: back to the radar */
#define STREAM_FRAMES   60                  /* a strip at most every second, between track fetches */
#define LAG_RELOAD      40                  /* cells streaming may fall behind before a whole new patch */
#define RELOAD_RETRY    600                 /* frames before trying a failed whole patch again */
#define STRIP           8                   /* cells per streamed strip */

struct ck_terrain ck_terrain;               /* cached on both SH-2s: the master purges on terrain_gen */
static u8 strip[2 * STRIP * AR_TN];

static const u8 *icao;
static struct ar_track trk;                 /* last good fix */
static u32 fix_at, req_at, strip_at, reload_at;
static s32 load_z;
static s16 wx, wy;                          /* the maps hold grid cells [wx, wx + 128) x [wy, wy + 128) */

static void status(u8 st) { sh.ck.status = st; }

static void hud_line(u16 i, const char *s)
{
    u16 n = 0;
    for (; s[n] && n < 39; n++) sh.ck.hud[i][n] = s[n];
    for (; n < 40; n++) sh.ck.hud[i][n] = 0;
}

static void hud(void)
{
    char s[48], a[16], b[16], cs[9];
    const struct ck_cam *c = &sh.ck.cam;

    memcpy(cs, trk.cs, 8);
    cs[8] = 0;
    for (s16 i = 7; i >= 0 && cs[i] == ' '; i--) cs[i] = 0;
    hud_line(0, cs);
    if (trk.kflags & AR_K_GROUND) strcpy(a, "GROUND");
    else if (trk.kflags & AR_K_ALT_UNK) strcpy(a, "--");
    else { fmt_thousands(b, (s32)trk.alt * 25); sprintf(a, "%s FT", b); }
    sprintf(s, "ALT %-11s GS %u KT", a, trk.gs);
    hud_line(1, s);
    fmt_thousands(b, trk.vrate);
    sprintf(s, "VS %s%-9s HDG %03u  BANK %c%u", trk.vrate > 0 ? "+" : "", b,
            (u16)(((u32)c->heading * 360 + 32768) >> 16), c->bank < 0 ? 'L' : 'R', abs(c->bank));
    hud_line(2, s);
}

static void message(const char *s)
{
    u16 n = 0;
    for (; s[n] && n < 39; n++) sh.ck.msg[n] = s[n];
    sh.ck.msg[n] = 0;
    for (u16 i = 0; i < 120; i++) mars_wait_frame();
}

/* ------------------------------------------------------------ data */

static void publish_patch(void)
{
    const struct ar_terrain *t = &ck_terrain.hdr;
    memcpy(sh.ck.pal, t->pal, sizeof(sh.ck.pal));
    /* screen rows per level at 1 cell: 160 px focal length (a 90 degree view) x level / cell, Q8 */
    u32 k = t->cell_m ? (u32)t->level_cm * 4096 / ((u32)t->cell_m * 10) : 2560;
    sh.ck.k_q8 = k > 16384 ? 16384 : k;
    sh.ck.hmax = t->hmax;
    sh.ck.terrain_gen++;
}

static bool load_terrain(void)
{
    status(CK_LOADING);
    bool ok = feed_terrain(icao, &ck_terrain.hdr, ck_terrain.h, ck_terrain.c) == FEED_OK &&
              ck_terrain.hdr.magic[0] == 'A' && ck_terrain.hdr.magic[1] == 'T';
    if (ok) {
        wx = wy = 0;
        publish_patch();
    }
    return ok;
}

/* Fetch a fix and post it; the master steers onto it (snap: put the camera there at once). */
static bool get_fix(bool snap)
{
    struct ar_track t;
    struct ck_fix f;

    req_at = vtimer;
    if (feed_track(icao, ck_terrain.hdr.key, &t) != FEED_OK || !(t.kflags & AR_K_FOUND)) return FALSE;

    u32 frames = vtimer - fix_at;
    s16 hd_rate = 0;
    if (!snap && frames > 0 && frames < 30 * 60) hd_rate = (s16)(t.heading - trk.heading) / (s16)frames;
    trk = t;
    fix_at = vtimer;
    f.vx = (s32)t.vx * 256 / 60;
    f.vy = (s32)t.vy * 256 / 60;
    f.vz = (s32)t.vz * 256 / 60;
    f.hd_rate = hd_rate;
    f.heading = t.heading + (u16)(hd_rate * 60 * t.age);
    f.x = (t.x << 8) + f.vx * 60 * t.age;   /* where it is now: the fix plus the time since it was heard */
    f.y = (t.y << 8) + f.vy * 60 * t.age;
    f.z = (t.z << 8) + f.vz * 60 * t.age;
    f.bank = t.bank;
    s16 pitch = 0;                          /* rows: 160 px focal x vrate / gs (ft/min over kt is x 101) */
    if (t.gs > 30) pitch = (s16)((s32)160 * t.vrate / ((s32)t.gs * 101));
    f.horizon = VOX_HORIZON + (pitch < -40 ? -40 : pitch > 40 ? 40 : pitch);
    f.snap = snap;
    sh.ck.fix = f;
    sh.ck.fix_seq++;
    if (sh.ck.status != CK_LOADING) status(CK_LIVE);
    return TRUE;
}

static s16 lag_x(void) { return (s16)(sh.ck.cam.x >> 16) - AR_TN / 2 - wx; }
static s16 lag_y(void) { return (s16)(sh.ck.cam.y >> 16) - AR_TN / 2 - wy; }

static bool need_patch(void)
{
    s32 z = sh.ck.cam.z;
    bool scale = z > load_z * 2 + (16L << 16) || z < load_z / 2 - (16L << 16);
    return scale || abs(lag_x()) > LAG_RELOAD || abs(lag_y()) > LAG_RELOAD;
}

/* One strip toward keeping the maps centred on the camera; the bigger lag first (src/cockpit.c). */
static void stream(void)
{
    s16 lx = lag_x(), ly = lag_y(), x, y;
    u16 w, h;
    bool cols = abs(lx) >= abs(ly);

    strip_at = vtimer;
    if (abs(lx) < STRIP && abs(ly) < STRIP) return;
    if (cols) { w = STRIP; h = AR_TN; y = wy; x = lx > 0 ? wx + AR_TN : wx - STRIP; }
    else { w = AR_TN; h = STRIP; x = wx; y = ly > 0 ? wy + AR_TN : wy - STRIP; }
    if (feed_strip(ck_terrain.hdr.key, x, y, w, h, strip) != FEED_OK) return;

    const u8 *sh_ = strip, *sc = strip + w * h;
    for (u16 r = 0; r < h; r++) {
        u16 row = ((y + r) & (AR_TN - 1)) << 7;
        for (u16 k = 0; k < w; k++) {
            u16 i = row | ((x + k) & (AR_TN - 1));
            ck_terrain.h[i] = *sh_++;
            ck_terrain.c[i] = *sc++;
        }
    }
    if (cols) wx += lx > 0 ? STRIP : -STRIP;
    else wy += ly > 0 ? STRIP : -STRIP;
    sh.ck.terrain_gen++;
}

static void wait_release(void)
{
    while (mars_pad() & BUTTON_ALL) mars_wait_frame();
}

/* ------------------------------------------------------------ mode */

void cockpit_run(const u8 icao_in[3])
{
    icao = icao_in;
    memset(&trk, 0, sizeof(trk));
    memset(sh.ck.hud, 0, sizeof(sh.ck.hud));
    sh.ck.msg[0] = 0;
    status(CK_LOADING);
    view_set(VIEW_COCKPIT);
    wait_release();

    if (!load_terrain()) {
        message("NO TERRAIN FROM THE RELAY");
        goto out;
    }
    fix_at = vtimer;
    if (!get_fix(TRUE)) {
        message("AIRCRAFT NOT IN THE FEED");
        goto out;
    }
    mars_wait_frame();
    mars_wait_frame();                      /* the master has taken the snap: cam is valid */
    load_z = sh.ck.fix.z;
    reload_at = vtimer - RELOAD_RETRY;
    status(CK_LIVE);
    strip_at = vtimer - STREAM_FRAMES / 2;  /* strips half way between track fetches */

    while (TRUE) {
        mars_wait_frame();
        if (mars_pad() & BUTTON_B) break;
        u32 now = vtimer;
        if (now - req_at >= TRACK_FRAMES) {
            if (!get_fix(FALSE) && sh.ck.status == CK_LIVE) status(CK_STALE);
            if (vtimer - fix_at > LOST_SECS * 60UL) {
                status(CK_LOST);
                message("LOST THE AIRCRAFT: BACK TO THE RADAR");
                break;
            }
        }
        if (need_patch() && vtimer - reload_at >= RELOAD_RETRY) {
            reload_at = vtimer;
            if (load_terrain() && get_fix(TRUE)) {
                mars_wait_frame();
                mars_wait_frame();
                load_z = sh.ck.fix.z;
            }
            status(CK_LIVE);
        } else if (vtimer - strip_at >= STREAM_FRAMES) {
            stream();
        }
        hud();
    }
out:
    wait_release();
    view_set(VIEW_BLANK);
}
