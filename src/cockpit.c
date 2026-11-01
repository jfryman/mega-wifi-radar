/* Cockpit mode (lode/ui/cockpit.md): load a terrain patch centred on the aircraft, then fetch its track
 * every TRACK_FRAMES and fly the camera along it between fixes, rendering with voxel.c. A new fix's error
 * is absorbed over BLEND frames (or snapped, if large). A new patch is fetched when the aircraft drifts
 * RECENTRE cells from the patch centre or its height leaves the patch's scale. All requests block, so the
 * view pauses while one runs; the camera catches up by elapsed time afterwards. */
#include "cockpit.h"
#include "feed.h"
#include "fmt.h"
#include "voxel.h"

#define TRACK_FRAMES    120                 /* a track every 2 s */
#define LOST_SECS       20                  /* no good fix this long: back to the radar */
#define BLEND           32                  /* frames to absorb a fix's correction */
#define SNAP            (6L << 16)          /* corrections bigger than this (16.16 cells) jump */
#define CENTRE          (AR_TN / 2 * 65536L)
#define RECENTRE        (12L << 16)         /* drift from the patch centre before a new patch */
#define HUD_COL         1

enum { ST_LIVE, ST_STALE, ST_LOADING, ST_LOST };

static const u8 *icao;
static struct ar_terrain hdr;
static struct ar_track trk;                 /* last good fix */
static u32 fix_at, req_at, load_x, load_y;  /* vtimer of the last good fix / request; position at the last patch */
static s32 load_z;
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

static void hud_status(u8 st)
{
    static const char *const text[] = { "LIVE        ", "NO SIGNAL   ", "TERRAIN...  ", "SIGNAL LOST " };
    status = st;
    VDP_setTextPalette(st == ST_LIVE ? PAL1 : PAL3);
    VDP_drawText(text[st], 27, 17);
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
        sprintf(s, "TERRAIN %u M CELLS   %u.%u FPS", hdr.cell_m, fps10 / 10, fps10 % 10);
        hud_text(23, s);
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
    if (ok) hdr = nh;
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

/* A new patch centred on the aircraft: when it drifts off the middle of this one, or its height above
 * the ground no longer suits the cell size. Not again until it has moved as far from the last attempt
 * (an emulator pack, or a relay that can't move the patch, would otherwise reload every frame). */
static bool need_patch(void)
{
    bool off = abs(cam.x - CENTRE) > RECENTRE || abs(cam.y - CENTRE) > RECENTRE;
    bool moved = abs(cam.x - (s32)load_x) > RECENTRE || abs(cam.y - (s32)load_y) > RECENTRE;
    bool scale = z16 > load_z * 2 + (16L << 16) || z16 < load_z / 2 - (16L << 16);
    return (off && moved) || scale;
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

    voxel_init();
    VDP_setTextPlane(WINDOW);
    VDP_setTextPalette(PAL1);
    PAL_setColor(PAL1 * 16 + 15, 0xEEE);    /* HUD white, callsign cyan, warnings amber */
    PAL_setColor(PAL2 * 16 + 15, 0xEE0);
    PAL_setColor(PAL3 * 16 + 15, 0x0AE);
    hud_text(26, "B  BACK TO RADAR");
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
    load_x = cam.x; load_y = cam.y; load_z = z16;
    hud_status(ST_LIVE);

    u32 last = vtimer, win = vtimer;
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
        if (need_patch()) {
            load_x = cam.x; load_y = cam.y; load_z = z16;
            if (load_terrain() && get_fix(TRUE)) {
                load_x = cam.x; load_y = cam.y; load_z = z16;
            }
            hud_status(ST_LIVE);
            last = vtimer;
            advance(0);
        }

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
    wait_release();
    VDP_setTextPlane(BG_A);
}
