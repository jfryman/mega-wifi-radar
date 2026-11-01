/* Cockpit mode (lode/ui/cockpit.md): load a terrain patch centred on the aircraft, then fetch its track
 * every TRACK_FRAMES and fly the camera along it between fixes, rendering with voxel.c. A new fix's error
 * is absorbed over BLEND frames (or snapped, if large). As the aircraft moves, strips of new cells stream
 * into the wrapping buffer (at most one per STREAM_FRAMES), keeping it centred on the camera; a whole new
 * patch only when its height leaves the patch's scale, or streaming has fallen far behind. All requests
 * block, so the view pauses while one runs (a strip ~0.2 s, a patch ~2.5 s); the camera catches up by
 * elapsed time afterwards. A flyover notice (lode/ui/notices.md) never takes the view over: OVERHEAD! in the
 * status slot and its three lines in turn on the credit row, fetched in a strip's slot. */
#include "cockpit.h"
#include "chime.h"
#include "feed.h"
#include "fmt.h"
#include "notice.h"
#include "voxel.h"

#define TRACK_FRAMES    120                 /* a track every 2 s */
#define LOST_SECS       20                  /* no good fix this long: back to the radar */
#define BLEND           32                  /* frames to absorb a fix's correction */
#define SNAP            (6L << 16)          /* corrections bigger than this (16.16 cells) jump */
#define STREAM_FRAMES   60                  /* a strip at most every second, between track fetches */
#define LAG_RELOAD      40                  /* cells streaming may fall behind before a whole new patch */
#define RELOAD_RETRY    600                 /* frames before trying a failed whole patch again */
#define HUD_COL         1
#define CREDIT          "ELEVATION DATA: AWS TERRAIN TILES"     /* attribution, as the radar credits adsb.fi */

enum { ST_LIVE, ST_STALE, ST_LOADING, ST_LOST };

static const u8 *icao;
static struct ar_terrain hdr;
static struct ar_track trk;                 /* last good fix */
static u32 fix_at, req_at, strip_at, reload_at;    /* vtimer of the last good fix / requests */
static s32 load_z;                          /* height when the patch was loaded */
static s16 wx, wy;                          /* the buffer holds grid cells [wx, wx + 128) x [wy, wy + 128) */
static struct vox_cam cam;
static s32 z16;                             /* camera height, 16.16 levels */
static s32 vx, vy, vz;                      /* per frame, 16.16 */
static s32 ex, ey, ez;                      /* correction per frame while blending */
static u16 blend;
static u16 hd_target;
static s16 hd_rate;                         /* heading change per frame between fixes */
static s16 horizon_target;
static u8 status;
static u16 fps10;
static const struct ar_notice *nt;          /* the notice on the HUD */
static u16 nt_id;
static u32 nt_at;
static s8 nt_page;                          /* line on the credit row; -1 before the first */

/* ------------------------------------------------------------ HUD (window plane, rows 16-27) */

static void hud_text(u16 row, const char *s)
{
    char line[39];
    u16 n = 0;
    while (s[n] && n < sizeof(line) - 1) { line[n] = s[n]; n++; }
    while (n < sizeof(line) - 1) line[n++] = ' ';
    line[n] = 0;
    VDP_drawText(line, HUD_COL, row);
}

/* The notice is about the aircraft we're riding. */
static bool riding(void) { return (nt->nflags & AR_N_ICAO) && !memcmp(nt->icao, icao, 3); }

static void hud_status(u8 st)
{
    static const char *const text[] = { "LIVE        ", "NO SIGNAL   ", "TERRAIN...  ", "SIGNAL LOST " };
    status = st;
    VDP_setTextPalette(st == ST_LIVE && !nt ? PAL1 : PAL3);
    VDP_drawText(st == ST_LIVE && nt ? (riding() ? "OVER HOME!  " : "OVERHEAD!   ") : text[st], 27, 17);
    VDP_setTextPalette(PAL1);
}

/* Every frame: start a new notice, turn its page (secs / 3 each), then give the credit row back. */
static void hud_notice(void)
{
    char s[AR_NOTICE_COLS + 1];

    if (!nt || nt->id != nt_id) {           /* none yet, or a newer one replaced it */
        if (!(nt = notice_current())) return;
        nt_id = nt->id;
        nt_at = vtimer;
        nt_page = -1;
        hud_status(status);
    }
    u32 span = nt->secs * 20UL, t = vtimer - nt_at;
    s8 page = t >= 3 * span ? 3 : t / span;
    if (page == nt_page) return;
    nt_page = page;
    if (page == 3) {
        notice_done();
        nt = NULL;
        hud_text(25, CREDIT);
        hud_status(status);
        return;
    }
    if (page == 0 && riding()) strcpy(s, "YOU ARE OVER HOME!");
    else {
        memcpy(s, nt->text + page * AR_NOTICE_COLS, AR_NOTICE_COLS);
        s[AR_NOTICE_COLS] = 0;
    }
    VDP_setTextPalette(page == 0 ? PAL3 : PAL1);
    hud_text(25, s);
    VDP_setTextPalette(PAL1);
}

/* One line per call (part 0-3): text costs render time, so it's spread over frames. */
static void hud(u16 part)
{
    char s[48], a[16], b[16];

    switch (part) {
    case 0: {
        char cs[9];
        memcpy(cs, trk.cs, 8);
        cs[8] = 0;
        VDP_setTextPalette(PAL2);
        VDP_drawText(cs, HUD_COL, 17);
        VDP_setTextPalette(PAL1);
        break;
    }
    case 1:
        if (trk.kflags & AR_K_GROUND) strcpy(a, "GROUND");
        else if (trk.kflags & AR_K_ALT_UNK) strcpy(a, "--");
        else { fmt_thousands(b, (s32)trk.alt * 25); sprintf(a, "%s FT", b); }
        sprintf(s, "ALT %-12s GS %u KT", a, trk.gs);
        hud_text(19, s);
        break;
    case 2:
        fmt_thousands(b, trk.vrate);
        sprintf(s, "VS %s%-9s HDG %03u   BANK %c%u", trk.vrate > 0 ? "+" : "", b,
                (u16)(((u32)cam.heading * 360 + 32768) >> 16), cam.bank < 0 ? 'L' : 'R', abs(cam.bank));
        hud_text(21, s);
        break;
    default:
        sprintf(s, "%2u.%u FPS", fps10 / 10, fps10 % 10);
        VDP_drawText(s, 30, 23);            /* right of "B  BACK TO RADAR" */
        break;
    }
}

/* ------------------------------------------------------------ data */

/* A failed download can leave the maps part new (raw), part old (converted): voxel_patch's conversion is
 * idempotent, so it runs either way and vox.s never sees a raw height (an odd offset: an address error). */
static bool load_terrain(void)
{
    static struct ar_terrain nh;
    bool ok;

    hud_status(ST_LOADING);
    ok = feed_terrain(icao, &nh, VOX->h, VOX->c) == FEED_OK;
    if (ok) {
        hdr = nh;
        wx = wy = 0;                        /* a new grid: its cells 0..127 */
    }
    PAL_setColors(PAL0 * 16, hdr.pal, 16, CPU);
    voxel_patch(&hdr);
    return ok;
}

/* Fetch a fix and steer the camera onto it; snap puts it there at once (a new patch, or the first fix). */
static bool get_fix(bool snap)
{
    struct ar_track t;

    req_at = vtimer;
    if (feed_track(icao, hdr.key, &t) != FEED_OK || !(t.kflags & AR_K_FOUND)) return FALSE;

    u32 frames = vtimer - fix_at;           /* since the previous fix */
    if (!snap && frames > 0 && frames < 30 * 60)
        hd_rate = (s16)(t.heading - trk.heading) / (s16)frames;
    else hd_rate = 0;
    trk = t;
    fix_at = vtimer;
    vx = (s32)t.vx * 256 / 60;
    vy = (s32)t.vy * 256 / 60;
    vz = (s32)t.vz * 256 / 60;
    hd_target = t.heading + (u16)(hd_rate * 60 * t.age);

    /* where it is now: the fix plus the time since upstream heard it */
    s32 tx = (t.x << 8) + vx * 60 * t.age, ty = (t.y << 8) + vy * 60 * t.age, tz = (t.z << 8) + vz * 60 * t.age;
    s32 dx = tx - cam.x, dy = ty - cam.y, dz = tz - z16;
    if (snap || abs(dx) > SNAP || abs(dy) > SNAP || abs(dz) > (32L << 16)) {
        cam.x = tx; cam.y = ty; z16 = tz;
        cam.heading = hd_target;
        blend = 0;
    } else {
        ex = dx / BLEND; ey = dy / BLEND; ez = dz / BLEND;
        blend = BLEND;
    }

    s16 pitch = 0;                          /* rows: 160 px focal x vrate / gs (ft/min over kt is x 101) */
    if (t.gs > 30) pitch = (s16)((s32)160 * t.vrate / ((s32)t.gs * 101));
    horizon_target = VOX_HORIZON + (pitch < -30 ? -30 : pitch > 30 ? 30 : pitch);
    if (status != ST_LOADING) hud_status(ST_LIVE);
    return TRUE;
}

/* How far the buffer lags behind being centred on the camera, in cells. */
static s16 lag_x(void) { return (s16)(cam.x >> 16) - AR_TN / 2 - wx; }
static s16 lag_y(void) { return (s16)(cam.y >> 16) - AR_TN / 2 - wy; }

/* A whole new patch: when the aircraft's height no longer suits the cell size (it has halved or doubled),
 * or streaming has fallen too far behind (failed strips). Streaming handles ordinary movement. */
static bool need_patch(void)
{
    bool scale = z16 > load_z * 2 + (16L << 16) || z16 < load_z / 2 - (16L << 16);
    return scale || abs(lag_x()) > LAG_RELOAD || abs(lag_y()) > LAG_RELOAD;
}

/* One strip toward keeping the buffer centred: VOX_STRIP columns (or rows) ahead replace those furthest
 * behind, which the wrap puts in the same place. The bigger lag goes first. A failed strip changes nothing
 * that matters (its cells are converted as they're spread) and is asked for again next time. */
static void stream(void)
{
    s16 lx = lag_x(), ly = lag_y(), x, y;
    u16 w, h;
    bool cols = abs(lx) >= abs(ly);

    strip_at = vtimer;
    if (abs(lx) < VOX_STRIP && abs(ly) < VOX_STRIP) return;
    if (cols) { w = VOX_STRIP; h = AR_TN; y = wy; x = lx > 0 ? wx + AR_TN : wx - VOX_STRIP; }
    else { w = AR_TN; h = VOX_STRIP; x = wx; y = ly > 0 ? wy + AR_TN : wy - VOX_STRIP; }
    if (feed_strip(hdr.key, x, y, w, h, VOX->strip) != FEED_OK) return;

    const u8 *sh = VOX->strip, *sc = VOX->strip + w * h;
    for (u16 r = 0; r < h; r++) {
        u16 row = ((y + r) & (AR_TN - 1)) << 7;
        for (u16 k = 0; k < w; k++) {
            u16 i = row | ((x + k) & (AR_TN - 1));
            VOX->h[i] = *sh++ & 0x3E;       /* as voxel_patch: (level / 2) * 2, colour in both nibbles */
            VOX->c[i] = (*sc++ & 0x0F) * 0x11;
        }
    }
    if (cols) wx += lx > 0 ? VOX_STRIP : -VOX_STRIP;
    else wy += ly > 0 ? VOX_STRIP : -VOX_STRIP;
}

static void advance(u16 frames)
{
    while (frames--) {
        cam.x += vx;
        cam.y += vy;
        z16 += vz;
        if (blend) { cam.x += ex; cam.y += ey; z16 += ez; blend--; }
        hd_target += hd_rate;
        cam.heading += (s16)(hd_target - cam.heading) / 8;
        if (cam.bank < trk.bank) cam.bank++;
        else if (cam.bank > trk.bank) cam.bank--;
        if (cam.horizon < horizon_target) cam.horizon++;
        else if (cam.horizon > horizon_target) cam.horizon--;
    }
    s32 h = z16 >> 16;
    cam.h = h > 2000 ? 2000 : h < -2000 ? -2000 : h;
}

static void wait_release(void)
{
    while (JOY_readJoypad(JOY_1) & (BUTTON_ALL)) SYS_doVBlankProcess();
}

static void message(const char *s)
{
    hud_text(25, s);
    for (u16 i = 0; i < 120; i++) SYS_doVBlankProcess();
}

/* ------------------------------------------------------------ mode */

void cockpit_run(const u8 icao_in[3])
{
    icao = icao_in;
    memset(&trk, 0, sizeof(trk));
    memset(&cam, 0, sizeof(cam));
    cam.horizon = horizon_target = VOX_HORIZON;
    status = ST_LOADING;
    fps10 = 0;
    nt = NULL;

    voxel_init();
    VDP_setTextPlane(WINDOW);
    VDP_setTextPalette(PAL1);
    PAL_setColor(PAL1 * 16 + 15, 0xEEE);    /* HUD white, callsign cyan, warnings amber */
    PAL_setColor(PAL2 * 16 + 15, 0xEE0);
    PAL_setColor(PAL3 * 16 + 15, 0x0AE);
    hud_text(23, "B  BACK TO RADAR");
    hud_text(25, CREDIT);
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
    load_z = z16;
    reload_at = vtimer - RELOAD_RETRY;
    hud_status(ST_LIVE);

    u32 last = vtimer, win = vtimer;
    strip_at = vtimer - STREAM_FRAMES / 2;  /* strips half way between track fetches */
    u16 frames = 0, part = 0;
    while (TRUE) {
        u32 now = vtimer;
        advance(now - last);
        last = now;
        if (JOY_readJoypad(JOY_1) & BUTTON_B) break;

        if (now - req_at >= TRACK_FRAMES) {
            if (!get_fix(FALSE) && status == ST_LIVE) hud_status(ST_STALE);
            if (vtimer - fix_at > LOST_SECS * 60UL) {
                hud_status(ST_LOST);
                message("LOST THE AIRCRAFT: BACK TO THE RADAR");
                break;
            }
        }
        if (need_patch() && vtimer - reload_at >= RELOAD_RETRY) {
            reload_at = vtimer;             /* a failing relay isn't asked every frame */
            if (load_terrain() && get_fix(TRUE)) load_z = z16;
            hud_status(ST_LIVE);
            last = vtimer;
            advance(0);
        } else if (vtimer - strip_at >= STREAM_FRAMES) {
            if (feed_notice_waiting()) {    /* the notice instead of a strip this time */
                strip_at = vtimer;
                notice_check();
            } else {
                stream();
            }
        }

        chime_tick();
        hud_notice();
        voxel_render(&cam);
        hud(part);
        part = (part + 1) & 3;
        voxel_flip();
        if (++frames == 16) {
            fps10 = (u16)(600UL * frames / (u16)(vtimer - win));
            frames = 0;
            win = vtimer;
        }
    }
out:
    if (nt) notice_done();                  /* seen: the radar doesn't show it again */
    wait_release();
    VDP_setTextPlane(BG_A);
}
