/* Master SH-2: cockpit mode's view (lode/mars/renderer.md; the Genesis design is lode/ui/cockpit.md). Voxel
 * Space over the patch: 160 columns x 160 rows, front to back with a y-buffer, bank as a per-column shear,
 * pitch moving the horizon. 256 colours buy what the Genesis couldn't: every terrain colour in 8 fog steps that
 * blend into the haze with distance, and a sky gradient. The master also flies the camera between the fixes the
 * slave posts (src/cockpit.c's advance()), so the view runs at the frame rate however long a request takes. */
#include "render.h"
#include "draw.h"
#include "mars.h"
#include "shared.h"
#include "rvox.h"

#define C_TERR      16      /* 16 + colour * 8 + fog */
#define FOG         8
#define C_SKY       160     /* 160..223: sky, zenith to the horizon haze */
#define N_SKY       64
#define C_HUD_BG    224
#define C_HUD_EDGE  225
#define C_HUD_LIVE  226
#define C_HUD_WARN  227
#define C_RETICLE   228
#define C_TAPE      229
#define BLEND       32
#define SNAP        (6L << 16)
#define NZ_MAX      72

static const s16 sin_q14[256] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
    16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143, 16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
    15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842, 13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
    11585, 11297, 11003, 10702, 10394, 10080, 9760, 9434, 9102, 8765, 8423, 8076, 7723, 7366, 7005, 6639,
    6270, 5897, 5520, 5139, 4756, 4370, 3981, 3590, 3196, 2801, 2404, 2006, 1606, 1205, 804, 402,
    0, -402, -804, -1205, -1606, -2006, -2404, -2801, -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
    -6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765, -9102, -9434, -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395, -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986, -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143, -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842, -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080, -9760, -9434, -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
    -6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590, -3196, -2801, -2404, -2006, -1606, -1205, -804, -402,
};

static u16 zt[NZ_MAX], nz;
static u8 zfog[NZ_MAX];
static s16 top[VOX_W], roll[VOX_W];
static s16 ytab[AR_LEVELS];
static u16 gen_seen, fix_seen;
static u32 last;
static struct ck_cam cam;               /* the camera as flown; copied to sh.ck.cam each frame */
static struct ck_fix fx;
static s32 ex, ey, ez;
static u16 blend;
static u16 fps_frames;
static u32 fps_win;

/* ------------------------------------------------------------ palette */

static void palette(void)
{
    static const u16 txt[4] = { RGB(30, 30, 30), RGB(30, 30, 30), RGB(8, 28, 31), RGB(31, 20, 4) };
    u16 deep = rgb_genesis(sh.ck.pal[1]), mid = rgb_genesis(sh.ck.pal[2]), pale = rgb_genesis(sh.ck.pal[3]);
    u16 haze = rgb_mix(pale, RGB(28, 28, 30), 1, 3);
    for (u16 i = 0; i < N_SKY; i++)
        pal_set(C_SKY + i, i < 32 ? rgb_mix(deep, mid, i, 32) : rgb_mix(mid, haze, i - 32, 32));
    for (u16 c = 0; c < 16; c++) {
        u16 base = rgb_genesis(sh.ck.pal[c]);
        for (u16 f = 0; f < FOG; f++) pal_set(C_TERR + c * FOG + f, rgb_mix(base, haze, f * f, (FOG + 1) * (FOG + 1) / 2));
    }
    pal_set(0, 0);
    pal_set(C_HUD_BG, RGB(1, 2, 4));
    pal_set(C_HUD_EDGE, RGB(6, 10, 18));
    pal_set(C_HUD_LIVE, RGB(6, 26, 8));
    pal_set(C_HUD_WARN, RGB(31, 18, 2));
    pal_set(C_RETICLE, RGB(10, 31, 12));
    pal_set(C_TAPE, RGB(20, 28, 20));
    text_palette(txt);
}

void rvox_enter(void)
{
    u16 z = 192, n = 0;                         /* depth steps, Q8 cells: 0.75 .. 64 (src/voxel.c) */
    while (z < 64 * 256 && n < NZ_MAX) {
        zt[n++] = z;
        z += (z >> (z < 16 * 256 ? 3 : 2)) + 16;
    }
    nz = n;
    for (u16 k = 0; k < nz; k++) zfog[k] = (u8)((u32)k * k * FOG / ((u32)nz * nz + 1));
    memset(&cam, 0, sizeof(cam));
    cam.horizon = VOX_HORIZON;
    blend = 0;
    gen_seen = sh.ck.terrain_gen - 1;           /* palette + purge on the first frame */
    fix_seen = sh.ck.fix_seq;
    last = mars_vtimer;
    fps_frames = 0;
    fps_win = mars_vtimer;
    sh.ck.fps10 = 0;
}

/* ------------------------------------------------------------ camera (src/cockpit.c: get_fix + advance) */

static void take_fix(void)
{
    fx = sh.ck.fix;
    s32 dx = fx.x - cam.x, dy = fx.y - cam.y, dz = fx.z - cam.z;
    if (fx.snap || abs(dx) > SNAP || abs(dy) > SNAP || abs(dz) > (32L << 16)) {
        cam.x = fx.x; cam.y = fx.y; cam.z = fx.z;
        cam.heading = fx.heading;
        blend = 0;
    } else {
        ex = dx / BLEND; ey = dy / BLEND; ez = dz / BLEND;
        blend = BLEND;
    }
}

static void advance(u16 frames)
{
    while (frames--) {
        cam.x += fx.vx;
        cam.y += fx.vy;
        cam.z += fx.vz;
        if (blend) { cam.x += ex; cam.y += ey; cam.z += ez; blend--; }
        fx.heading += fx.hd_rate;
        cam.heading += (s16)(fx.heading - cam.heading) / 8;
        if (cam.bank < fx.bank) cam.bank++;
        else if (cam.bank > fx.bank) cam.bank--;
        if (cam.horizon < fx.horizon) cam.horizon++;
        else if (cam.horizon > fx.horizon) cam.horizon--;
    }
}

/* ------------------------------------------------------------ view */

static void render(void)
{
    const s16 hx = sin_q14[cam.heading >> 8], hy = sin_q14[(u8)((cam.heading >> 8) + 64)];   /* forward: +y north */
    const s16 rx = hy, ry = -hx;                                                            /* right */
    s16 horizon = cam.horizon < 16 ? 16 : cam.horizon > VOX_H - 24 ? VOX_H - 24 : cam.horizon;
    s16 bank = cam.bank < -60 ? -60 : cam.bank > 60 ? 60 : cam.bank;
    u8 bi = (u8)(s16)(bank * 256 / 360);
    s32 slope = -(s32)sin_q14[bi] * 512 / sin_q14[(u8)(bi + 64)];      /* Q8 rows per column: tan x 2 px */
    s32 acc = -(VOX_W / 2) * slope;
    for (u16 i = 0; i < VOX_W; i++) { roll[i] = acc >> 8; top[i] = VOX_H; acc += slope; }

    const s32 ch = cam.z >> 16;
    const u32 kq8 = sh.ck.k_q8;
    const s32 above = ch - sh.ck.hmax;
    const u32 z_min = above > 0 ? (u32)above * kq8 / (VOX_H - horizon) : 0;
    const u8 *H = ck_terrain.h, *C = ck_terrain.c;
    u16 k0 = 0;
    while (k0 < nz && zt[k0] < z_min) k0++;

    for (u16 k = k0; k < nz; k++) {
        const s32 z = zt[k];
        u32 s = (kq8 << 8) / z;                         /* rows per level at this depth, Q8 */
        s32 base = ((s32)horizon << 8) + ch * (s32)s;
        if (base > 0x3FFFFFF || base < -0x3FFFFFF) continue;
        for (u16 h = 0; h < AR_LEVELS; h++) ytab[h] = (base - (s32)h * (s32)s) >> 8;
        s32 px = cam.x + ((z * (hx - rx)) >> 6);        /* left edge of the view, 16.16 cells */
        s32 py = cam.y + ((z * (hy - ry)) >> 6);
        const s32 dx = (z * rx) / (VOX_W * 32), dy = (z * ry) / (VOX_W * 32);
        const u8 fog = C_TERR + zfog[k];
        for (u16 i = 0; i < VOX_W; i++, px += dx, py += dy) {
            u16 cell = (((py >> 16) & (AR_TN - 1)) << 7) | ((px >> 16) & (AR_TN - 1));
            s16 y = ytab[H[cell] & (AR_LEVELS - 1)] + roll[i];
            s16 t = top[i];
            if (y >= t) continue;
            if (y < 0) y = 0;
            u8 col = fog + (C[cell] & 15) * FOG;
            u16 w = col << 8 | col;
            volatile u16 *p = FB_WD + y * 160 + i;
            for (s16 r = y; r < t; r++, p += 160) *p = w;
            top[i] = y;
        }
    }
    for (u16 i = 0; i < VOX_W; i++) {                   /* sky: a gradient that follows the horizon */
        volatile u16 *p = FB_WD + i;
        s16 h0 = horizon + roll[i];
        for (s16 y = 0; y < top[i]; y++, p += 160) {
            s16 d = y - h0 + N_SKY - 8;
            u8 col = C_SKY + (d < 0 ? 0 : d >= N_SKY ? N_SKY - 1 : d);
            *p = col << 8 | col;
        }
    }
}

static void overlay(void)
{
    /* nose reticle, with the bank shown by its wings */
    const s16 cx = 160, cy = VOX_HORIZON;      /* fixed to the airframe: the horizon moves, not the nose */
    fb_hline(cx - 22, cx - 8, cy, C_RETICLE);
    fb_hline(cx + 8, cx + 22, cy, C_RETICLE);
    fb_vline(cx - 8, cy, cy + 4, C_RETICLE);
    fb_vline(cx + 8, cy, cy + 4, C_RETICLE);
    fb_px(cx, cy, C_RETICLE);
    /* heading tape along the top: 2 px per degree, ticks every 5, labels every 30 */
    u32 h10 = ((u32)cam.heading * 3600) >> 16;          /* tenths of a degree */
    fb_rect(80, 2, 160, 12, C_HUD_BG);
    for (s16 deg = (s16)(h10 / 10) - 40; deg <= (s16)(h10 / 10) + 40; deg++) {
        if (deg % 5) continue;
        s16 x = 160 + (s16)(((s32)deg * 10 - (s32)h10) / 5);
        if (x < 82 || x > 238) continue;
        s16 d = (deg % 360 + 360) % 360;
        if (d % 10) { fb_px(x, 12, C_TAPE); continue; }
        fb_vline(x, 10, 13, C_TAPE);
        if (d % 30) continue;
        char b[4];
        static const char *const card[4] = { "N", "E", "S", "W" };
        if (d % 90 == 0) strcpy(b, card[d / 90]);
        else sprintf(b, "%u", d / 10);
        s16 tx = x - (b[1] ? 8 : 4);
        if (tx >= 82 && tx + 16 <= 238) fb_text(tx, 2, b, C_TAPE, 0);
    }
    fb_vline(160, 2, 14, C_HUD_WARN);
}

static void hud(void)
{
    static const char *const st[4] = { "LIVE", "NO SIGNAL", "TERRAIN...", "SIGNAL LOST" };
    char b[16];
    fb_rect(0, VOX_H, FB_W, FB_H - VOX_H, C_HUD_BG);
    fb_hline(0, FB_W - 1, VOX_H, C_HUD_EDGE);
    fb_text_big(8, VOX_H + 5, sh.ck.hud[0], 2, C_TXT(2), C_SHADOW);
    u8 s = sh.ck.status & 3;
    s16 w = strlen(st[s]) * 8 + 8;
    fb_rect(FB_W - 8 - w, VOX_H + 6, w, 12, s == CK_LIVE ? C_HUD_LIVE : C_HUD_WARN);
    fb_text(FB_W - 4 - w, VOX_H + 8, st[s], C_TXT(0), 0);
    fb_text(8, VOX_H + 24, sh.ck.hud[1], C_TXT(0), 0);
    fb_text(8, VOX_H + 34, sh.ck.hud[2], C_TXT(0), 0);
    fb_text(8, VOX_H + 46, "B  BACK TO RADAR", C_TXT(2), 0);
    sprintf(b, "%2u.%u FPS", sh.ck.fps10 / 10, sh.ck.fps10 % 10);
    fb_text(FB_W - 8 - 8 * 8, VOX_H + 46, b, C_DIM, 0);
    if (sh.ck.msg[0]) fb_text(8, VOX_H + 56, sh.ck.msg, C_TXT(3), 0);
    else fb_text(8, VOX_H + 56, "ELEVATION DATA: AWS TERRAIN TILES", C_DIM, 0);
}

void rvox_frame(void)
{
    if (sh.ck.terrain_gen != gen_seen) {
        gen_seen = sh.ck.terrain_gen;
        cache_purge();                          /* the slave rewrote the maps under our cache */
        palette();
    }
    if (sh.ck.fix_seq != fix_seen) {
        fix_seen = sh.ck.fix_seq;
        take_fix();
    }
    u32 now = mars_vtimer;
    advance(now - last);
    last = now;
    sh.ck.cam = cam;

    if (sh.ck.status == CK_LOADING && !sh.ck.fix_seq) {
        fb_clear(C_SKY + N_SKY / 2);
    } else {
        render();
        overlay();
    }
    hud();
    if (++fps_frames == 16) {
        sh.ck.fps10 = (u16)(600UL * fps_frames / (mars_vtimer - fps_win + 1));
        fps_frames = 0;
        fps_win = mars_vtimer;
    }
}
