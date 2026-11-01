/*
 * Cockpit-mode speed spike (lode/plans/cockpit-mode.md): how fast can the 68000 draw Voxel Space terrain?
 * Standalone ROM, no MegaWiFi. Procedural terrain from tools/voxelgen (in ROM; a RAM patch reads at the same speed).
 *
 * 80 logical columns x 112 rows, each logical pixel 4 screen pixels wide = one VDP word. Spans are written
 * straight to VRAM through the data port with auto-increment 4 (tiles are column-major, so a column's rows
 * are 4 bytes apart): no RAM frame buffer, no DMA. Two VRAM copies, flipped with plane A's vertical scroll.
 * Only sky rows uncovered since a copy's last frame are repainted. HUD on the window plane.
 * Near depth steps sample every column; past Z_LOD one map sample serves two columns (vox_pairs), and past
 * Z_MID the depth step doubles. The inner loops are 68000 assembly (vox.s).
 * A: autopilot on/off. B: depth step (coarse/medium/fine). D-pad: turn (banks) and climb/descend.
 */
#include <genesis.h>
#include "terrain.h"

#define VC          80                      /* logical columns, 4 px each */
#define FB_H        112
#define FB_TW       (VC / 2)                /* 40 tile columns */
#define FB_TH       (FB_H / 8)              /* 14 tile rows */
#define COL_STRIDE  (FB_H * 4)              /* bytes per tile column */

#define VIEW_ROW    2                       /* copy 0 on plane rows 2-15, copy 1 on 18-31 */
#define HUD_ROW     16                      /* window plane from here down */
#define HORIZON     (FB_H / 2 - 12)

#define H_LEVELS    32                      /* row table entries: height / 2 */
#define K_PIX       20                      /* screen px per height level at 1 cell (2x exaggerated) */
#define NZ_MAX      120
#define Z_NEAR      192                     /* 0.75 cell, 8.8 */
#define Z_FAR       (96 * 256)
#define Z_LOD       (6 * 256)               /* from here one sample per two columns */
#define Z_MID       (16 * 256)              /* from here the depth step doubles */

#define SPEED       1100                    /* cells per vblank, Q16 (~0.017) */
#define MIN_CLEAR   10                      /* levels above the ground under the camera */

struct col { s16 top; s16 roll; u32 cmd; };   /* layout shared with vox.s; top is minus roll */
static struct col cols_buf[2][VC];              /* per VRAM copy: cmd is set once at start-up */
static s16 ytab[H_LEVELS];
static u16 zt[NZ_MAX];
static u16 nz;
static u16 steps;                           /* depth steps actually walked last frame */
static u16 z_shift = 3;                     /* near: dz = z >> z_shift + 1/16 cell; past Z_MID twice that */
static u16 sky_word[FB_H];
static u32 rowcmd[FB_H];                    /* row y's offset (y * 4) in command form: y << 18 */
static s16 prev_top[2][VC];                 /* per copy: highest terrain row last time it was drawn */

/* camera: x in Q16 cells, y in Q23 cells (so y >> 16 is row * 128), heading 65536 per turn */
static u32 cam_x = 64UL << 16, cam_y = 64UL << 23;
static u16 heading = 0;
static s16 cam_h = 72;                      /* above the highest peak, as a real flight would be */
static s16 turn;                            /* heading change per vblank */
static bool autopilot = TRUE;

/* Time in scanlines since boot (262 per NTSC frame), counted from the start of a vblank. */
static u32 lines_now(void)
{
    u32 vt;
    u16 v;
    do {
        vt = vtimer;
        v = *(vu8 *)(VDP_HVCOUNTER_PORT);           /* V counter: 0x00-0xEA, then 0xE5-0xFF */
    } while (vt != vtimer);
    if (v > 0xEA) v -= 6;                           /* make it 0..261 */
    v = v >= 224 ? v - 224 : v + 38;                /* lines since vblank start */
    return vt * 262 + v;
}

static inline s16 sinq(u16 a) { return sin_q14[a >> 8]; }
static inline s16 cosq(u16 a) { return sin_q14[(u8)((a >> 8) + 64)]; }

static void depth_table(void)
{
    u16 z = Z_NEAR;
    nz = 0;
    while (z < Z_FAR && nz < NZ_MAX) {
        zt[nz++] = z;
        z += (z >> (z < Z_MID ? z_shift : z_shift - 1)) + 16;
    }
}

/* vox.s: one depth step across the columns, spans straight into VRAM (auto-increment 4). */
void vox_step(u32 px, u32 py, s32 dx, s32 dy, struct col *cols, const s16 *ytab, u16 n);
void vox_pairs(u32 px, u32 py, s32 dx, s32 dy, struct col *cols, const s16 *ytab, u16 npairs);
void vox_ytab(s16 *ytab, s32 v, s32 dv);         /* 32 unclamped entries */

/* 32 / 16 -> 16 unsigned divide (the quotient must fit 16 bits): one divu, not libgcc's __udivsi3. */
static inline u16 divu16(u32 num, u16 den)
{
    __asm__("divu.w %1,%0" : "+d"(num) : "dm"(den) : "cc");
    return (u16)num;
}

static void render(u16 b)
{
    const s16 fx = sinq(heading), fy = cosq(heading);       /* forward */
    const s16 rx = fy, ry = -fx;                            /* right */
    const s16 rxs = (s32)rx * 128 / (VC / 2), rys = (s32)ry * 128 / (VC / 2);
    const s16 slope = -(turn * 6);                          /* horizon shear per logical column, Q8 */
    const s16 shear = (s16)(((s32)abs(slope) * (VC / 2) >> 8) + 1); /* largest |roll| */

    struct col *const cols = cols_buf[b];
    s16 acc = -(VC / 2) * slope;                            /* roll of column i, Q8, by accumulation */
    for (struct col *c = cols; c < cols + VC; c++) {
        const s16 r = acc >> 8;
        c->roll = r;
        c->top = FB_H - r;                                  /* "unrolled": compared directly with ytab */
        acc += slope;
    }

    /* nothing nearer than this can reach the screen: the tallest terrain at that depth is at the bottom row */
    const s16 above = cam_h - TERRAIN_HMAX;
    const u16 z_min = above > 0 ? (u16)((u32)above * K_PIX * 256 / (FB_H - HORIZON)) : 0;
    u16 k0 = 0;
    while (k0 < nz && zt[k0] < z_min) k0++;
    steps = nz - k0;

    VDP_setAutoInc(4);
    for (u16 k = k0; k < nz; k++) {
        const s16 z = zt[k];
        u32 px = cam_x + ((s32)z * (s16)(fx - rx) >> 6);
        u32 py = cam_y + ((s32)z * (s16)(fy - ry) << 1);
        const s32 dx = (s32)z * rxs >> 13;
        const s32 dy = (s32)z * rys >> 6;

        /* screen y for every height level at this depth */
        const u16 sc = divu16(K_PIX * 65536UL, (u16)z);
        /* 16.16, so each row is the high word (a swap, not a 32-bit shift) */
        /* clamped at the largest shear: y + roll >= 0, so vox.s never clips */
        s32 v = ((s32)HORIZON << 16) + ((s32)(s16)cam_h * (s16)sc << 8);
        const s32 dv = (s32)sc << 9, v_min = (s32)shear << 16;
        if (v - (dv << 5) + dv >= v_min) vox_ytab(ytab, v, dv);   /* lowest entry fine: none clamp */
        else {
            s16 *yt = ytab;
            for (u16 h = H_LEVELS; h; h--) {
                if (v < v_min) v = v_min;
                *yt++ = (s16)(v >> 16);
                v -= dv;
            }
        }

        if (z < Z_LOD) vox_step(px, py, dx, dy, cols, ytab, VC);
        else vox_pairs(px + (dx >> 1), py + (dy >> 1), dx * 2, dy * 2, cols, ytab, VC / 2);
    }

    /* sky only where this copy had terrain last time and doesn't now */
    s16 *pt = prev_top[b];
    for (u16 i = 0; i < VC; i++) {
        const s16 top = cols[i].top + cols[i].roll;
        if (pt[i] < top) {
            *(vu32 *)VDP_CTRL_PORT = cols[i].cmd + rowcmd[pt[i]];
            for (s16 y = pt[i]; y < top; y++) *(vu16 *)VDP_DATA_PORT = sky_word[y];
        }
        pt[i] = top;
    }
    VDP_setAutoInc(2);
}

static void fly(u16 vb)
{
    const u16 joy = JOY_readJoypad(JOY_1);
    static u16 prev;
    if ((joy & BUTTON_A) && !(prev & BUTTON_A)) autopilot = !autopilot;
    if ((joy & BUTTON_B) && !(prev & BUTTON_B)) { z_shift = z_shift == 5 ? 2 : z_shift + 1; depth_table(); }
    prev = joy;

    if (autopilot) turn = 14;               /* ~78 s per circle */
    else turn = joy & BUTTON_RIGHT ? 24 : joy & BUTTON_LEFT ? -24 : 0;
    if (joy & BUTTON_UP) cam_h++;
    if (joy & BUTTON_DOWN) cam_h--;

    for (u16 t = 0; t < vb; t++) {
        heading += turn;
        cam_x += (s32)sinq(heading) * SPEED >> 14;
        cam_y += ((s32)cosq(heading) * SPEED >> 14) << 7;
    }
    const u16 under = terrain_h[((u16)(cam_x >> 16) & 127) | ((u16)(cam_y >> 16) & 0x3F80)];
    if (cam_h < (s16)under + MIN_CLEAR) cam_h = under + MIN_CLEAR;
    if (cam_h > 160) cam_h = 160;
}

/* Right-aligned decimal into s[0..w-1], space padded; no sprintf (too slow for the frame budget). */
static void put_u(char *s, u16 w, u16 v)
{
    for (s16 i = w - 1; i >= 0; i--) {
        s[i] = (i == w - 1 || v) ? '0' + v % 10 : ' ';
        v /= 10;
    }
}

/* One HUD line per call (part 0-2): text costs render time, so it's spread over frames. */
static void hud(u16 part, u16 fps10, u16 ln_render, u16 ln_max)
{
    if (part == 0) {
        static char s[] = "FPS 00.0  RENDER 000 LN (MAX 000)";
        put_u(s + 4, 2, fps10 / 10);
        s[7] = '0' + fps10 % 10;
        put_u(s + 17, 3, ln_render);
        put_u(s + 29, 3, ln_max);
        VDP_drawText(s, 1, HUD_ROW + 3);
    } else if (part == 1) {
        static char s[] = "HDG 000   ALT 000   DEPTH STEPS 000";
        const u16 hdg = (u16)((u32)heading * 360 >> 16);
        s[4] = '0' + hdg / 100; s[5] = '0' + hdg / 10 % 10; s[6] = '0' + hdg % 10;
        put_u(s + 14, 3, cam_h);
        put_u(s + 32, 3, steps);
        VDP_drawText(s, 1, HUD_ROW + 5);
    } else {
        VDP_drawText(autopilot ? "AUTOPILOT  (A: OFF)       " : "MANUAL  (A: AUTOPILOT)    ", 1, HUD_ROW + 7);
    }
}

int main(bool hard)
{
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_clearPlane(WINDOW, TRUE);
    VDP_setWindowHPos(FALSE, 0);
    VDP_setWindowVPos(TRUE, HUD_ROW);
    VDP_setTextPlane(WINDOW);
    VDP_setTextPalette(PAL1);
    PAL_setPalette(PAL0, terrain_pal, CPU);
    PAL_setColor(16 + 15, 0xEEE);
    VDP_setBackgroundColor(0);

    /* Tile columns are 14 consecutive tiles; one that would straddle a 16 KB boundary (512 tiles) starts
     * past it instead, so a column's command + row offset never carries into the address's top bits. */
    u16 t = TILE_USER_INDEX;
    for (u16 b = 0; b < 2; b++) {
        for (u16 tx = 0; tx < FB_TW; tx++) {
            if ((t & ~511) != ((t + FB_TH - 1) & ~511)) t = (t + FB_TH - 1) & ~511;
            for (u16 ty = 0; ty < FB_TH; ty++)
                VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, t + ty), tx, VIEW_ROW + b * 16 + ty);
            for (u16 h = 0; h < 2; h++) cols_buf[b][tx * 2 + h].cmd = VDP_WRITE_VRAM_ADDR((u32)t * 32 + h * 2);
            t += FB_TH;
        }
        for (u16 i = 0; i < VC; i++) prev_top[b][i] = 0;    /* first frame paints all the sky */
    }
    for (u16 y = 0; y < FB_H; y++) rowcmd[y] = (u32)y << 18;
    for (u16 y = 0; y < FB_H; y++) {
        const u16 c = y < HORIZON / 3 ? SKY_DEEP : y < (2 * HORIZON) / 3 ? SKY_MID : SKY_PALE;
        sky_word[y] = c | (c << 8);
    }

    VDP_drawText("COCKPIT SPIKE   VOXEL SPACE 80X112 > VRAM", 0, HUD_ROW + 1);
    VDP_drawText("B: DEPTH STEP   D-PAD: TURN / CLIMB", 1, HUD_ROW + 9);
    depth_table();

    u16 back = 1, frames = 0, ln_max = 0, ln_shown = 0;
    u32 t_win = vtimer, t_prev = vtimer;
    u16 fps10 = 0;
    while (TRUE) {
        const u32 t0 = vtimer, l0 = lines_now();
        fly(t0 - t_prev);
        t_prev = t0;
        render(back);
        const u16 ln = lines_now() - l0;              /* 262 lines = one frame */
        if (ln > ln_max) ln_max = ln;

        if (frames < 3) hud(frames, fps10, ln, ln_shown);   /* in the slack before the flip, not after it */
        SYS_doVBlankProcess();                              /* flip at the start of a vblank: no tearing */
        VDP_setVerticalScroll(BG_A, back ? 128 : 0);
        back ^= 1;
        const u32 t2 = vtimer;

        if (++frames == 8) {                                /* fps and worst render over 8 frames */
            fps10 = (u16)(600UL * frames / (u16)(t2 - t_win));
            frames = 0;
            ln_shown = ln_max;
            ln_max = 0;
            t_win = t2;
        }
    }
    return 0;
}
