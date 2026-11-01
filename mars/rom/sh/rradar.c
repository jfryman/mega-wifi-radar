/* Master SH-2: the radar (lode/mars/renderer.md), the 32X counterpart of the Genesis V-Int renderer
 * (src/radar.c; lode/ui/screen-layout.md). Same layout, scale and rules: the sweep turns one step (1/256 turn)
 * per frame, each aircraft's new data is applied when the sweep crosses its bearing, five trail positions,
 * tags placed in the first free spot of four. Redrawn whole every frame: a 256-colour disc with a long phosphor
 * afterglow, glowing blips, faded trails, leader-lined tags, the side panel and the detail pop-up. */
#include "render.h"
#include "draw.h"
#include "mars.h"
#include "shared.h"
#include "geo.h"
#include "scope.h"
#include "settings.h"
#include "theme.h"
#include "traffic.h"

#define CX          (SCOPE_X + SCOPE_CX)        /* 112 */
#define CY          (SCOPE_Y + SCOPE_CY)
#define R           SCOPE_R                     /* 104 */
#define GLOW_LINES  56                          /* afterglow: lines half a step apart, newest last */

/* palette */
#define C_BG        0
#define C_DISC0     1       /* 1..8: the disc, centre to rim */
#define K_RING      10
#define K_RING_DIM  11
#define C_TICK      12
#define C_LABEL     13
#define C_GLOW      16      /* 16..47: afterglow, oldest .. newest */
#define N_GLOW      32
#define C_BLIP      48      /* 48..55: blip brightness, bright .. dim */
#define C_BLIP_R    56      /* 56..63: emergency */
#define C_HALO      64
#define C_HALO_R    65
#define K_STALE     66
#define K_SELECT    67
#define K_TRAIL     68      /* 68..72: newest .. oldest */
#define C_LEADER    73
#define C_PANEL     80
#define C_PANEL_HI  81
#define C_PANEL_EDGE 82
#define C_POPUP     83
#define C_POPUP_EDGE 84
#define C_SEL_BAR   85

#define NO_DOT      (-32768)

static u8 step;                         /* sweep, 0 = north, clockwise */
static u32 last;
static u16 k_scale;
static u8 theme_now = 0xFF;

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

/* ------------------------------------------------------------ palette */

static void palette(u8 th)
{
    bool stars = th == THEME_STARS;
    static const u16 txt_classic[4] = { RGB(29, 31, 29), RGB(8, 31, 10), RGB(8, 28, 31), RGB(31, 8, 6) };
    static const u16 txt_stars[4] = { RGB(31, 31, 31), RGB(24, 24, 24), RGB(10, 28, 31), RGB(31, 8, 6) };

    pal_set(C_BG, 0);
    if (stars) {
        pal_ramp(C_DISC0, 8, RGB(1, 1, 3), RGB(0, 0, 1));
        pal_set(K_RING, RGB(10, 12, 20));
        pal_set(K_RING_DIM, RGB(5, 6, 11));
        pal_set(C_TICK, RGB(12, 14, 22));
        pal_set(C_LABEL, RGB(14, 16, 24));
        pal_ramp(C_GLOW, N_GLOW, RGB(0, 0, 1), RGB(0, 0, 1));
        pal_ramp(C_BLIP, 8, RGB(31, 31, 31), RGB(31, 31, 31));
        pal_set(C_HALO, RGB(10, 10, 14));
        pal_ramp(K_TRAIL, 5, RGB(18, 18, 22), RGB(6, 6, 9));
        pal_set(C_LEADER, RGB(14, 14, 18));
        text_palette(txt_stars);
    } else {
        pal_ramp(C_DISC0, 8, RGB(0, 6, 2), RGB(0, 2, 1));
        pal_set(K_RING, RGB(4, 20, 6));
        pal_set(K_RING_DIM, RGB(1, 10, 3));
        pal_set(C_TICK, RGB(6, 24, 8));
        pal_set(C_LABEL, RGB(8, 22, 10));
        for (u16 i = 0; i < N_GLOW; i++) {     /* a long phosphor tail: dark green to the bright edge */
            u16 t = i * i / N_GLOW;
            pal_set(C_GLOW + i, i == N_GLOW - 1 ? RGB(22, 31, 22) : rgb_mix(RGB(0, 3, 1), RGB(2, 26, 6), t, N_GLOW));
        }
        pal_ramp(C_BLIP, 8, RGB(24, 31, 24), RGB(2, 10, 3));
        pal_set(C_HALO, RGB(2, 12, 4));
        pal_ramp(K_TRAIL, 5, RGB(4, 22, 7), RGB(1, 7, 2));
        pal_set(C_LEADER, RGB(4, 16, 6));
        text_palette(txt_classic);
    }
    pal_ramp(C_BLIP_R, 8, RGB(31, 14, 12), RGB(10, 1, 1));
    pal_set(C_HALO_R, RGB(12, 2, 2));
    pal_set(K_STALE, RGB(14, 14, 14));
    pal_set(K_SELECT, RGB(31, 22, 4));
    pal_set(C_PANEL, stars ? RGB(1, 1, 4) : RGB(1, 4, 2));
    pal_set(C_PANEL_HI, stars ? RGB(3, 4, 10) : RGB(2, 8, 4));
    pal_set(C_PANEL_EDGE, stars ? RGB(8, 10, 20) : RGB(3, 16, 6));
    pal_set(C_POPUP, stars ? RGB(1, 1, 5) : RGB(0, 3, 2));
    pal_set(C_POPUP_EDGE, stars ? RGB(10, 14, 26) : RGB(4, 22, 8));
    pal_set(C_SEL_BAR, stars ? RGB(4, 6, 14) : RGB(2, 10, 4));
    theme_now = th;
}

void rradar_enter(void)
{
    palette(settings.theme);
    k_scale = geo_scale(settings_range_nm());
    step = 0;
    last = mars_vtimer;
}

/* ------------------------------------------------------------ tracks (V-Int logic, src/radar.c) */

static void apply(struct track *t)
{
    if (t->shown) {
        for (u16 n = TRAIL_N - 1; n > 0; n--) {
            t->trail_x[n] = t->trail_x[n - 1];
            t->trail_y[n] = t->trail_y[n - 1];
        }
        t->trail_x[0] = t->cur.x;
        t->trail_y[0] = t->cur.y;
        if (t->trail_n < TRAIL_N) t->trail_n++;
    }
    t->cur = t->next;
    memcpy(t->tag, t->next_tag, sizeof(t->tag));
    t->tag_pal = t->next_pal;
    t->has_next = 0;
    t->shown = 1;
    t->hit_step = step;
}

/* Advance the sweep by the frames since the last call; apply what it crosses. */
static void sweep(void)
{
    u32 now = mars_vtimer;
    u16 n = now - last;
    last = now;
    if (n > 64) n = 64;
    lock_take(0);
    if (sh.req_rescale) { sh.req_rescale = 0; k_scale = geo_scale(settings_range_nm()); }
    if (sh.req_theme) { sh.req_theme = 0; }
    sh.req_retag = 0;
    while (n--) {
        step++;
        if (step == 0) sh.rotations++;
        for (u16 i = 0; i < TR_MAX; i++) {
            u8 st = tr_state[i];
            if (!st) continue;
            struct track *t = &tracks[i];
            if (st & TS_DROP) {
                memset(t, 0, sizeof(*t));
                t->tag_x = t->tag_y = -1;
                tr_state[i] = 0;
                if (sh.selected == (s16)i) sh.selected = -1;
                continue;
            }
            if ((st & TS_NEXT) && tr_brg[i] == step) {
                apply(t);
                tr_state[i] = st & ~TS_NEXT;
            }
        }
    }
    lock_give(0);
    if (theme_now != settings.theme) palette(settings.theme);
}

/* ------------------------------------------------------------ scope */

static s16 half[R + 1];                 /* the disc's half width at |dy| */

static void disc(void)
{
    if (!half[0]) for (s16 y = 0; y <= R; y++) {
        s16 w = 0;
        while ((s32)(w + 1) * (w + 1) <= (s32)R * R - (s32)y * y) w++;
        half[y] = w;
    }
    for (s16 y = -R; y <= R; y++) {
        u16 ay = y < 0 ? -y : y;
        fb_hline(CX - half[ay], CX + half[ay], CY + y, C_DISC0 + ay * 8 / (R + 1));
    }
}

/* a line from the centre to the rim at angle a (65536 per turn, 0 = north, clockwise) */
static void ray(u16 a, u8 c, s16 r0)
{
    s16 s = sin_q14[a >> 8], co = sin_q14[(u8)((a >> 8) + 64)];
    s32 fs = s, fc = co;
    for (s16 r = r0; r < R; r++)
        FB_PX[(CY - (s16)((fc * r) >> 14)) * FB_W + CX + (s16)((fs * r) >> 14)] = c;
}

static void afterglow(void)
{
    if (!themes[settings.theme].sweep) return;
    u16 a = (u16)step << 8;
    for (u16 i = 0; i < GLOW_LINES; i++) {
        u16 age = GLOW_LINES - 1 - i;                    /* oldest first */
        ray(a - age * 128, C_GLOW + (N_GLOW - 1) - (u16)((u32)age * N_GLOW / GLOW_LINES), age > 24 ? 20 : 2);
    }
}

static void rings(void)
{
    char buf[8];
    for (u16 k = 1; k <= 5; k++) {
        s16 r = R * k / 5;
        if (k == 5) fb_circle(CX, CY, r, K_RING);
        else fb_dotted_circle(CX, CY, r, K_RING_DIM, 2);
    }
    for (u16 a = 0; a < 256; a += 8) {                  /* ticks every 11.25 degrees, longer every 45 */
        s16 s = sin_q14[a], c = sin_q14[(u8)(a + 64)], l = (a & 31) ? 4 : 8;
        for (s16 r = R - l; r < R; r++) fb_px(CX + (s16)((s32)s * r >> 14), CY - (s16)((s32)c * r >> 14), C_TICK);
    }
    fb_hline(CX - 3, CX + 3, CY, C_TICK);               /* home */
    fb_vline(CX, CY - 3, CY + 3, C_TICK);
    fb_text(CX - 3, SCOPE_Y - 6 + 0, "N", C_LABEL, 0);
    fb_text(CX - 3, CY + R - 3, "S", C_LABEL, 0);
    fb_text(CX - R - 6, CY - 4, "W", C_LABEL, 0);
    fb_text(CX + R - 1, CY - 4, "E", C_LABEL, 0);
    u16 nm = settings_range_nm();
    for (u16 k = 1; k <= 5; k++) {                      /* ring labels along the north-east diagonal */
        s16 r = R * k / 5;
        sprintf(buf, "%u", nm * k / 5);
        fb_text(CX + r * 181 / 256 + 2, CY - r * 181 / 256 - 9, buf, C_LABEL, 0);
    }
}

/* ------------------------------------------------------------ traffic */

static bool screen_pos(s16 x, s16 y, s16 *px, s16 *py)
{
    s16 dx = geo_px(x, k_scale), dy = geo_px(y, k_scale);
    *px = CX + dx;
    *py = CY - dy;
    return (s32)dx * dx + (s32)dy * dy <= (s32)(R - 2) * (R - 2);
}

static void trails(void)
{
    for (u16 i = 0; i < TR_MAX; i++) {
        struct track *t = &tracks[i];
        if (!t->used || !t->shown) continue;
        for (u16 n = 0; n < t->trail_n; n++) {
            s16 px, py;
            if (!screen_pos(t->trail_x[n], t->trail_y[n], &px, &py)) continue;
            fb_rect(px, py, 2, 2, K_TRAIL + n);
        }
    }
}

static void blip(s16 x, s16 y, u8 lvl, bool emerg, bool stale)
{
    u8 core = stale ? K_STALE : (emerg ? C_BLIP_R : C_BLIP) + lvl;
    if (lvl < 4 && !stale) {
        u8 halo = emerg ? C_HALO_R : C_HALO;
        fb_rect(x - 2, y - 1, 5, 3, halo);
        fb_rect(x - 1, y - 2, 3, 5, halo);
    }
    fb_rect(x - 1, y - 1, 3, 3, core);
    if (!lvl && !stale) fb_px(x, y, C_TXT(0));
}

struct box { s16 x, y; };
static struct box placed[TR_MAX];
static u16 nplaced;

static bool free_at(s16 x, s16 y)
{
    if (x < 2 || y < 2 || x + 64 > CX + R + 6 || y + 16 > CY + R + 6) return FALSE;
    for (u16 i = 0; i < nplaced; i++)
        if (x < placed[i].x + 68 && placed[i].x < x + 68 && y < placed[i].y + 18 && placed[i].y < y + 18) return FALSE;
    return TRUE;
}

static void tag(const struct track *t, s16 bx, s16 by, bool sel)
{
    static const s16 off[4][2] = { { 8, -18 }, { 8, 4 }, { -72, -18 }, { -72, 4 } };
    for (u16 k = 0; k < 4; k++) {
        s16 x = (bx + off[k][0]) & ~1, y = by + off[k][1];
        if (!free_at(x, y)) continue;
        placed[nplaced].x = x;
        placed[nplaced++].y = y;
        s16 lx = off[k][0] > 0 ? x : x + 64, ly = off[k][1] < 0 ? y + 16 : y;
        fb_line(bx + (off[k][0] > 0 ? 2 : -2), by + (off[k][1] < 0 ? -2 : 2), lx, ly, sel ? K_SELECT : C_LEADER);
        u8 fg = C_TXT(t->tag_pal & 3);
        fb_textn(x, y, t->tag[0], TAG_W, fg, 0);
        fb_textn(x, y + 8, t->tag[1], TAG_W, fg, 0);
        return;
    }
}

static void traffic(void)
{
    bool fade = themes[settings.theme].blip_fade;
    s16 sel = sh.selected;
    nplaced = 0;
    for (u16 pass = 0; pass < 2; pass++)                /* the selection's tag first: it gets the best spot */
        for (u16 i = 0; i < TR_MAX; i++) {
            struct track *t = &tracks[i];
            if (!t->used || !t->shown) continue;
            if ((pass == 0) != ((s16)i == sel)) continue;
            s16 px, py;
            if (!screen_pos(t->cur.x, t->cur.y, &px, &py)) continue;
            bool stale = t->cur.flags & AR_F_STALE, emerg = t->cur.flags & AR_F_EMERG;
            u8 lvl = fade && !stale ? ((u8)(step - t->hit_step)) >> 5 : 0;
            blip(px, py, lvl, emerg, stale);
            if ((s16)i == sel) {
                fb_frame(px - 6, py - 6, 13, 13, K_SELECT);
                fb_rect(px - 6, py - 6, 3, 3, K_SELECT);
                fb_rect(px + 4, py + 4, 3, 3, K_SELECT);
            }
            if (settings.tag_mode == TAGS_ALL || (settings.tag_mode == TAGS_SELECTED && (s16)i == sel))
                tag(t, px, py, (s16)i == sel);
        }
}

/* ------------------------------------------------------------ text */

static void panel(void)
{
    fb_rect(PANEL_COL * 8 - 4, 0, FB_W - PANEL_COL * 8 + 4, FB_H, C_PANEL);
    fb_vline(PANEL_COL * 8 - 5, 0, FB_H - 1, C_PANEL_EDGE);
    fb_rect(PANEL_COL * 8 - 4, 8, FB_W - PANEL_COL * 8 + 4, 8, C_PANEL_HI);    /* home airport row */
    for (u16 r = 0; r < PANEL_ROWS; r++) {
        if (!sh.panel[r][0]) continue;
        if (sh.panel_pal[r] == PAL2) fb_rect(PANEL_COL * 8 - 2, r * 8, PANEL_W * 8 + 4, 8, C_SEL_BAR);
        fb_textn(PANEL_COL * 8, r * 8, sh.panel[r], PANEL_W, C_TXT(sh.panel_pal[r] & 3), 0);
    }
}

static void popup(void)
{
    fb_rect(0, 0, POPUP_COLS * 8, FB_H, C_POPUP);
    fb_frame(2, 2, POPUP_COLS * 8 - 4, FB_H - 4, C_POPUP_EDGE);
    for (u16 r = 0; r < PANEL_ROWS; r++)
        fb_textn(0, r * 8, sh.popup[r], POPUP_COLS, C_TXT(sh.popup_pal[r] & 3), 0);
}

void rradar_frame(void)
{
    sweep();
    fb_clear(C_BG);
    if (!sh.popup_on) {
        disc();
        afterglow();
        rings();
        trails();
        traffic();
    }
    panel();
    if (sh.popup_on) popup();
}
