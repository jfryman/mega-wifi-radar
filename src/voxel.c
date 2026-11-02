/* Cockpit mode's terrain renderer (lode/ui/cockpit.md): Voxel Space into VRAM. The per-column work is in
 * vox.s; this file sets up the VDP, the per-depth row tables and the camera geometry.
 *
 * 80 logical columns x 112 rows, each logical pixel 4 screen pixels wide = one VDP word. Tiles are stored
 * column-major, so a column's rows are 4 bytes apart and a vertical span is one address command plus n
 * writes with the auto-increment at 4. Two VRAM copies; plane A maps copy 0 on rows 2-15 and copy 1 on rows
 * 18-31, and a vertical scroll of 0 or 128 picks one. Only sky uncovered since a copy's last frame is
 * repainted. Every depth step samples every column (Z_LOD = Z_FAR; vox_pairs, one sample for two, is unused). */
#include "voxel.h"

#define VIEW_ROW    2                       /* copy 0 on plane rows 2-15, copy 1 on 18-31 */
#define FB_TW       (VOX_COLS / 2)          /* 40 tile columns */
#define FB_TH       (VOX_ROWS / 8)          /* 14 tile rows */
#define Z_NEAR      192                     /* 0.75 cell, 8.8 */
#define Z_FAR       (64 * 256)              /* half the patch: beyond it the map wraps */
#define Z_LOD       Z_FAR                   /* one sample per two columns from here: never (every column sampled
                                             * is sharper far off for ~5-10% of the frame time; vox_pairs kept) */
#define Z_MID       (16 * 256)              /* the depth step doubles from here */
#define Z_SHIFT     3                       /* near: dz = z / 8 + 1/16 cell */
#define K_MAX       16384                   /* px per level at 1 cell, Q8: past this the patch is squashed */
#define SKY_DEEP    0x11                    /* palette 1-3, both nibbles */
#define SKY_MID     0x22
#define SKY_PALE    0x33

void vox_step(u32 px, u32 py, s32 dx, s32 dy, struct vox_col *cols, const s16 *ytab, u16 n,
              const u8 *h, const u8 *c);
void vox_pairs(u32 px, u32 py, s32 dx, s32 dy, struct vox_col *cols, const s16 *ytab, u16 npairs,
               const u8 *h, const u8 *c);

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

static u16 nz, steps, back, k_q8;
static u8 hmax;
static u32 rowcmd[VOX_ROWS];                /* row y's offset (y * 4) in command form: y << 18 */

static inline s16 sinq(u16 a) { return sin_q14[a >> 8]; }
static inline s16 cosq(u16 a) { return sin_q14[(u8)((a >> 8) + 64)]; }

/* Fixed bands, not following the horizon: the sky delta leaves rows it doesn't uncover as they were. */
static u8 sky(u16 y)
{
    return y < VOX_HORIZON / 3 ? SKY_DEEP : y < (2 * VOX_HORIZON) / 3 ? SKY_MID : SKY_PALE;
}

void voxel_init(void)
{
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_clearPlane(WINDOW, TRUE);
    VDP_setWindowHPos(FALSE, 0);
    VDP_setWindowVPos(TRUE, VOX_HUD_ROW);
    VDP_resetSprites();                     /* the radar's blips */
    VDP_updateSprites(1, CPU);
    VDP_setBackgroundColor(0);

    /* Tile columns are 14 consecutive tiles; one that would straddle a 16 KB boundary (512 tiles) starts
     * past it instead, so a column's command + row offset never carries into the address's top bits. */
    u16 t = TILE_USER_INDEX;
    for (u16 b = 0; b < 2; b++) {
        for (u16 tx = 0; tx < FB_TW; tx++) {
            if ((t & ~511) != ((t + FB_TH - 1) & ~511)) t = (t + FB_TH - 1) & ~511;
            VDP_fillTileData(0, t, FB_TH, TRUE);
            for (u16 ty = 0; ty < FB_TH; ty++)
                VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, t + ty), tx, VIEW_ROW + b * 16 + ty);
            for (u16 h = 0; h < 2; h++) VOX->cols[b][tx * 2 + h].cmd = VDP_WRITE_VRAM_ADDR((u32)t * 32 + h * 2);
            t += FB_TH;
        }
        for (u16 i = 0; i < VOX_COLS; i++) VOX->prev_top[b][i] = 0;     /* first frame paints all the sky */
    }
    for (u16 y = 0; y < VOX_ROWS; y++) rowcmd[y] = (u32)y << 18;

    u16 z = Z_NEAR;
    nz = 0;
    while (z < Z_FAR && nz < VOX_NZ_MAX) {
        VOX->zt[nz++] = z;
        z += (z >> (z < Z_MID ? Z_SHIFT : Z_SHIFT - 1)) + 16;
    }
    back = 1;
}

void voxel_patch(const struct ar_terrain *t)
{
    u8 *h = VOX->h, *c = VOX->c;

    for (u16 i = 0; i < AR_TN * AR_TN; i++) {
        h[i] &= 0x3E;                       /* level 0..63 -> (level / 2) * 2 */
        c[i] = (c[i] & 0x0F) * 0x11;
    }
    /* screen rows per level at 1 cell: 160 px focal length (a 90 degree view) x level / cell */
    u32 k = t->cell_m ? (u32)t->level_cm * 4096 / ((u32)t->cell_m * 10) : 2560;  /* 2560: level = cell / 16 */
    k_q8 = k > K_MAX ? K_MAX : k;
    hmax = t->hmax;
}

void voxel_render(const struct vox_cam *cam)
{
    const s16 fx = sinq(cam->heading), fy = cosq(cam->heading);   /* forward: north is +y */
    const s16 rx = fy, ry = -fx;                                    /* right */
    /* right per column, x 32 / 40 (Q14): at most 13,107, an s16 (x 128 overflowed it for every heading,
     * mirroring and stretching the view sideways); the shifts below take the other x 4 */
    const s16 rxs = (s32)rx * 32 / (VOX_COLS / 2), rys = (s32)ry * 32 / (VOX_COLS / 2);
    s16 horizon = cam->horizon;
    if (horizon < 8) horizon = 8;
    if (horizon > VOX_ROWS - 16) horizon = VOX_ROWS - 16;

    /* bank: the horizon shears by tan(bank) rows per screen px, 4 px per column; right bank lifts the right */
    s16 bank = cam->bank < -60 ? -60 : cam->bank > 60 ? 60 : cam->bank;
    u16 bi = (u16)(s16)(bank * 256 / 360) & 0xFF;
    const s16 slope = (s16)(-(s32)sin_q14[bi] * 1024 / sin_q14[(u8)(bi + 64)]);   /* Q8 rows per column */
    struct vox_col *const cols = VOX->cols[back];
    s16 acc = -(VOX_COLS / 2) * slope;
    for (struct vox_col *c = cols; c < cols + VOX_COLS; c++) {
        c->roll = acc >> 8;
        c->top = VOX_ROWS;
        acc += slope;
    }

    /* nothing nearer than this reaches the screen: the tallest terrain there is at the bottom row */
    const s16 above = cam->h - hmax;
    const u32 z_min = above > 0 ? (u32)above * k_q8 / (VOX_ROWS - horizon) : 0;
    u16 k0 = 0;
    while (k0 < nz && VOX->zt[k0] < z_min) k0++;
    steps = nz - k0;

    const u32 cy = (u32)cam->y << 7;
    VDP_setAutoInc(4);
    for (u16 k = k0; k < nz; k++) {
        const s16 z = VOX->zt[k];
        const u32 px = cam->x + ((s32)z * (s16)(fx - rx) >> 6);
        const u32 py = cy + ((s32)z * (s16)(fy - ry) << 1);
        const s32 dx = (s32)z * rxs >> 11;
        const s32 dy = (s32)z * rys >> 4;

        /* screen row of every second height level at this depth, 16.16 so each is a high word */
        u32 sc = ((u32)k_q8 << 8) / (u16)z;
        if (sc > 32767) sc = 32767;
        const s32 lift = (s32)cam->h * (s32)sc;
        if (lift > 8000000 || lift < -8000000) continue;        /* far off the screen; can't overflow below */
        s32 v = ((s32)horizon << 16) + (lift << 8);
        const s32 dv = (s32)sc << 9;
        s16 *yt = VOX->ytab;
        for (u16 h = VOX_LEVELS; h; h--) { *yt++ = (s16)(v >> 16); v -= dv; }

        if (z < Z_LOD) vox_step(px, py, dx, dy, cols, VOX->ytab, VOX_COLS, VOX->h, VOX->c);
        else vox_pairs(px + (dx >> 1), py + (dy >> 1), dx * 2, dy * 2, cols, VOX->ytab, VOX_COLS / 2,
                       VOX->h, VOX->c);
    }

    /* sky only where this copy had terrain last time and doesn't now */
    s16 *pt = VOX->prev_top[back];
    for (u16 i = 0; i < VOX_COLS; i++) {
        const s16 top = cols[i].top;
        if (pt[i] < top) {
            *(vu32 *)VDP_CTRL_PORT = cols[i].cmd + rowcmd[pt[i]];
            for (s16 y = pt[i]; y < top; y++) *(vu8 *)VDP_DATA_PORT = sky(y);
        }
        pt[i] = top;
    }
    VDP_setAutoInc(2);
}

void voxel_flip(void)
{
    SYS_doVBlankProcess();                  /* flip at the start of a vblank: no tearing */
    VDP_setVerticalScroll(BG_A, back ? 128 : 0);
    back ^= 1;
}

u16 voxel_steps(void) { return steps; }
