#include "chime.h"
#include "radar.h"
#include "geo.h"
#include "scope.h"
#include "settings.h"
#include "theme.h"
#include "traffic.h"

#define GRID_W      28      /* plane-A cells tracked for tag placement */
#define GRID_H      28
#define TAG_SPAN    (TAG_W + 1)     /* occupancy incl. a gap column */
#define MAX_BLIPS   79      /* + 1 selection box = 80 sprites */
#define REDRAW_PER_FRAME 2
#define PANEL_PER_FRAME  3
#define POPUP_PER_FRAME  3       /* 28-cell rows: the pop-up fills in ~10 frames before it shows */
#define C_OPAQUE         14      /* the scope's background in every palette of every theme: the pop-up's fill */
#define WX_PER_FRAME     3       /* weather rows redrawn per frame */
#define MAP_PX_PER_FRAME 96      /* video map pixels drawn per frame (whole lines: one may overrun) */
#define GAP              0x10    /* grid: a tag's gap column (empty on screen); the low nibble counts drawn cells */

/* Blip tiles: 4 brightness levels (colour index 1..4) + stale; then the
 * 16x16 selection brackets (4 tiles, column-major). */
static const u32 blip_tiles[5][8] = {
    { 0, 0, 0x00111100, 0x00111100, 0x00111100, 0x00111100, 0, 0 },
    { 0, 0, 0x00222200, 0x00222200, 0x00222200, 0x00222200, 0, 0 },
    { 0, 0, 0x00333300, 0x00333300, 0x00333300, 0x00333300, 0, 0 },
    { 0, 0, 0x00444400, 0x00444400, 0x00444400, 0x00444400, 0, 0 },
    { 0, 0, 0x00066000, 0x00600600, 0x00600600, 0x00066000, 0, 0 },
};
static const u32 box_tiles[4][8] = {
    /* top-left, bottom-left, top-right, bottom-right */
    { 0x55550000, 0x50000000, 0x50000000, 0x50000000, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0x50000000, 0x50000000, 0x50000000, 0x55550000 },
    { 0x00005555, 0x00000005, 0x00000005, 0x00000005, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0x00000005, 0x00000005, 0x00000005, 0x00005555 },
};

/* STARS data blocks (themes with `leader`): a 1-px leader line in the tag's colour (15), transparent elsewhere.
 * Diagonals run corner to corner, so cells in a row make one line. */
static const u32 lead_tiles[4][8] = {
    { 0x0000000F, 0x000000F0, 0x00000F00, 0x0000F000, 0x000F0000, 0x00F00000, 0x0F000000, 0xF0000000 },  /* rising: SW to NE */
    { 0xF0000000, 0x0F000000, 0x00F00000, 0x000F0000, 0x0000F000, 0x00000F00, 0x000000F0, 0x0000000F },  /* falling: NW to SE */
    { 0, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0 },  /* horizontal */
    { 0x000F0000, 0x000F0000, 0x000F0000, 0x000F0000, 0x000F0000, 0x000F0000, 0x000F0000, 0x000F0000 },  /* vertical */
};
enum { LT_SLASH, LT_BACK, LT_DASH, LT_BAR };

/* Weather stipple (lode/ui/weather.md), colour C_WX: light 25 %, moderate 50 %, heavy 75 % of the pixels. Periods
 * of 2 and 4 pixels, so neighbouring cells join up. */
#define WX_A 0xD000D000UL
#define WX_B 0x00D000D0UL
const u32 radar_wx_tiles[3][8] = {
    { WX_A, WX_B, WX_A, WX_B, WX_A, WX_B, WX_A, WX_B },
    { 0xD0D0D0D0, 0x0D0D0D0D, 0xD0D0D0D0, 0x0D0D0D0D, 0xD0D0D0D0, 0x0D0D0D0D, 0xD0D0D0D0, 0x0D0D0D0D },
    { ~WX_A & 0xDDDDDDDD, ~WX_B & 0xDDDDDDDD, ~WX_A & 0xDDDDDDDD, ~WX_B & 0xDDDDDDDD,
      ~WX_A & 0xDDDDDDDD, ~WX_B & 0xDDDDDDDD, ~WX_A & 0xDDDDDDDD, ~WX_B & 0xDDDDDDDD },
};
#undef WX_A
#undef WX_B
/* Directions, in the order they're tried: NE SE NW SW E W N S. The tag sits at (ltag) from the leader's last
 * cell, so the line meets its corner (diagonals) or its callsign row / first column. */
#define L_DIRS  8
static const s8 ldir[L_DIRS][2] = { { 1, -1 }, { 1, 1 }, { -1, -1 }, { -1, 1 }, { 1, 0 }, { -1, 0 }, { 0, -1 }, { 0, 1 } };
static const u8 ltile[L_DIRS] = { LT_SLASH, LT_BACK, LT_BACK, LT_SLASH, LT_DASH, LT_DASH, LT_BAR, LT_BAR };
static const s8 ltag[L_DIRS][2] = {
    { 1, -2 }, { 1, 1 }, { -TAG_W, -2 }, { -TAG_W, 1 }, { 1, 0 }, { -TAG_W, 0 }, { 0, -2 }, { 0, 1 },
};
#define LEAD_ON     0x80
#define LEAD_DIR(l) ((l) & 7)
#define LEAD_LEN(l) (((l) >> 3) & 3)

static u16 t_blip, t_box, t_ofont, t_lead, t_wx;
static u16 k_scale;
static u8 grid[GRID_H][GRID_W];         /* tag/label occupancy per plane-A cell */
static u8 label_bits[GRID_H * GRID_W / 8];     /* permanent: ring labels + compass, 1 bit per plane-A cell */
_Static_assert(GRID_W == 28 && GRID_H * GRID_W % 8 == 0, "label_bit's index is y * 28 by shifts");
static inline u16 label_bit(u16 x, u16 y) { return (y << 5) - (y << 2) + x; }   /* y * 28 + x, no multiply */
static inline u8 label_at(u16 x, u16 y) { u16 i = label_bit(x, y); return (label_bits[i >> 3] >> (i & 7)) & 1; }   /* 0 or 1: SGDK bool is a u8 */
static inline void label_set(u16 x, u16 y) { u16 i = label_bit(x, y); label_bits[i >> 3] |= 1 << (i & 7); }
/* Weather (lode/ui/weather.md): plane A cells without text show it, at low priority under the scope's plane B.
 * Cells as struct ar_wx sends them; wx_dirty: rows (bit r = scope row r + 1) to redraw. */
static u8 wx_cells[AR_WX_N * AR_WX_ROW];
static volatile u32 wx_dirty;
/* Video map (lode/ui/video-maps.md): segments in block pixels at the current range, drawn into the scope's fixed
 * pixels a few a frame (map_next up to map_n); a zoom or MAP off wipes them with the trail dots. */
static u16 map_buf[(AR_MAP_HDR + AR_MAP_MAX * sizeof(struct ar_seg)) / 2];   /* the /v1/map body, fetched in place */
#define map_segs    ((const struct ar_seg *)((const u8 *)map_buf + AR_MAP_HDR))
static volatile u16 map_n, map_next;
static u16 map_runways, map_centres;        /* the classes' ends in map_segs: runways, then centrelines, then ticks */
static volatile u8 req_mapclear;
static volatile s16 selected = -1;
static volatile u8 req_rescale, req_theme, req_retag;
static volatile u16 rotations;
static volatile u16 joy;
static u16 redraw_cursor;
static char panel_text[PANEL_ROWS][PANEL_W + 1];
static u8 panel_pal[PANEL_ROWS];
volatile u16 vint_lines_max;
volatile u32 vint_lines_sum;
volatile u32 prof_sum[8];
static inline u16 line_now(void) { u16 v = GET_VCOUNTER; return v >= 224 ? v - 224 : v + 38; }
#ifdef RADAR_DEBUG
#define PROF(i) do { u16 l_ = line_now(); prof_sum[i] += l_ - prof_l; prof_l = l_; } while (0)
#else
#define PROF(i) do { } while (0)
#endif
volatile u16 main_beat;
volatile u16 late_frames;


static volatile u32 panel_dirty;

/* Pop-up (lode/ui/detail-popup.md): the window plane over cols 0..27, drawn with an opaque copy of
 * the font so the scope doesn't show through. Rows are written while it's hidden; it appears once
 * they're all out, and closing is instant. Sprites hide while it's up. */
static char popup_text[PANEL_ROWS][POPUP_COLS];
static u8 popup_pal[PANEL_ROWS];
static volatile u32 popup_dirty;
static volatile u8 popup_want, popup_on;

/* Flyover notice box (lode/ui/notices.md): a bordered box on plane A over the scope's centre. It grows open
 * from the centre over BOX_STEPS steps, shows the notice, and shrinks shut. While it's up, its cells count as
 * occupied in the tag grid, so tags are placed around it; once shut, the labels and tags it covered come back.
 * A step's rows go out over BOX_FRAMES frames (at most 4 rows a frame). */
#define BOX_COLS    (AR_NOTICE_COLS + 2)    /* 26: cols 1..26 */
#define BOX_ROWS    7                       /* rows 11..17: border, 3 lines, blank, hint, border */
#define BOX_CX      14
#define BOX_CY      14
#define BOX_STEPS   4
#define BOX_FRAMES  2
static const u8 box_size[BOX_STEPS + 1][2] = { { 0, 0 }, { 4, 3 }, { 12, 5 }, { 20, 7 }, { BOX_COLS, BOX_ROWS } };
static char box_text[4][AR_NOTICE_COLS];    /* the 3 lines, then the hint */
static u8 box_pal[4];
static volatile u8 box_target;              /* 0 shut .. BOX_STEPS open */
static volatile u8 box_fast;                /* shut without waiting between steps (under the pop-up) */
static volatile u8 box_text_dirty;          /* text rows to write once open */
static volatile u8 box_cur;                 /* the step on screen */
static u8 box_from, box_phase;              /* the step being drawn over, and its frame */
static u16 t_frame;                         /* border tiles: TL T TR L fill R BL B BR */

/* Border tiles: a 2-pixel line in colour 15 (the palette's text colour) on opaque black, rounded corners. */
#define E 0xEEEEEEEEUL
static const u32 frame_tiles[9][8] = {
    { E, 0xEEEFFFFF, 0xEEFFFFFF, 0xEFFFEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE },  /* TL */
    { E, 0xFFFFFFFF, 0xFFFFFFFF, E, E, E, E, E },                                             /* T */
    { E, 0xFFFFFEEE, 0xFFFFFFEE, 0xEEEEFFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE },  /* TR */
    { 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE },  /* L */
    { E, E, E, E, E, E, E, E },                                                               /* fill */
    { 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE },  /* R */
    { 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFEEEEE, 0xEFFFEEEE, 0xEEFFFFFF, 0xEEEFFFFF, E },  /* BL */
    { E, E, E, E, E, 0xFFFFFFFF, 0xFFFFFFFF, E },                                             /* B */
    { 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEEFFE, 0xEEEEFFFE, 0xFFFFFFEE, 0xFFFFFEEE, E },  /* BR */
};
#undef E

/* ------------------------------------------------------------ helpers */

/* Direct tilemap row write: one address command, then one word per cell.
 * Plane A and the window are both 64 cells wide (radar_init sets 64x32).
 * SGDK's text calls build 128-entry buffers and cost ~10x more. */
static void row_write(u16 plane_addr, u16 x, u16 y, const char *s, u16 n, u16 attr)
{
    vu16 *data = (vu16 *)VDP_DATA_PORT;
    u16 base = attr + TILE_FONT_INDEX - 32;

    *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(plane_addr + ((((y) << 6) + (x)) << 1)));
    while (n--) {
        u8 c = *s++;
        *data = c ? base + c : 0;
    }
}

/* The plane A word for cell (x, y) without text: its weather tile, or blank. */
static u16 wx_word(s16 x, s16 y)
{
    if (x < 1 || y < 1 || x > AR_WX_N || y > AR_WX_N) return 0;
    u16 r = y - 1, c = x - 1;
    u8 l = (wx_cells[(r << 3) - r + (c >> 2)] >> (6 - ((c & 3) << 1))) & 3;
    return l ? TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, t_wx + l - 1) : 0;
}

/* Clear n plane A cells from (x, y): back to the weather under them. */
static void row_clear(u16 x, u16 y, u16 n)
{
    vu16 *data = (vu16 *)VDP_DATA_PORT;

    *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + ((((y) << 6) + (x)) << 1)));
    while (n--) *data = wx_word(x++, y);
}

/* A tag row: as row_write, but spaces show the weather under them. */
static void tag_row(u16 x, u16 y, const char *s, u16 n, u16 attr)
{
    vu16 *data = (vu16 *)VDP_DATA_PORT;
    u16 base = attr + TILE_FONT_INDEX - 32;

    *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + (((y << 6) + x) << 1)));
    while (n--) {
        u8 c = *s++;
        *data = c > ' ' ? base + c : wx_word(x, y);
        x++;
    }
}

/* Redraw dirty weather rows, at most WX_PER_FRAME a frame; cells holding text (tags, leaders, labels, the notice
 * box: the grid's low nibble) keep it. */
static void wx_rows(void)
{
    vu16 *data = (vu16 *)VDP_DATA_PORT;
    u16 done = 0;
    u32 bit = 1;

    for (u16 y = 1; y <= AR_WX_N && done < WX_PER_FRAME && wx_dirty; y++, bit <<= 1) {
        if (!(wx_dirty & bit)) continue;
        wx_dirty &= ~bit;
        bool addr = TRUE;
        for (u16 x = 1; x <= AR_WX_N; x++) {
            if (grid[y][x] & (GAP - 1)) { addr = TRUE; continue; }
            if (addr) {
                *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + (((y << 6) + x) << 1)));
                addr = FALSE;
            }
            *data = wx_word(x, y);
        }
        done++;
    }
}

/* The notice box's cells at an animation step (0: none). */
static void box_rect(u16 step, s16 *x, s16 *y, s16 *w, s16 *h)
{
    *w = box_size[step][0];
    *h = box_size[step][1];
    *x = BOX_CX - *w / 2;
    *y = BOX_CY - *h / 2;
}

static inline bool to_block(s16 x, s16 y, s16 *bx, s16 *by)
{
    s16 dx = geo_px(x, k_scale), dy = geo_px(y, k_scale);

    if ((s32)dx * dx + (s32)dy * dy > (s32)(SCOPE_R - 3) * (SCOPE_R - 3)) return FALSE;
    *bx = SCOPE_CX + dx;
    *by = SCOPE_CY - dy;
    return TRUE;
}

static void grid_mark(s16 x, s16 y, s16 w, s16 dv)
{
    for (s16 i = 0; i < w; i++)
        if (x + i >= 0 && x + i < GRID_W && y >= 0 && y < GRID_H) grid[y][x + i] += dv;
}

/* A tag's cells, and its gap column (counted as GAP: empty on screen, so weather still shows there). */
static void tag_mark(s16 x, s16 y, s16 dv)
{
    grid_mark(x, y, TAG_W, dv);
    grid_mark(x + TAG_W, y, 1, dv * GAP);
}

/* A tag's span includes a gap column on its right. A label directly left of it would
 * also read as part of the tag ("S" + "017" looks like "5017"), so check that cell too. */
static bool grid_free(s16 x, s16 y)
{
    if (x < 1 || y < 1 || x + TAG_W > 27 || y + 2 > 27) return FALSE;
    for (s16 r = 0; r < 2; r++) {
        if (label_at(x - 1, y + r)) return FALSE;
        for (s16 i = 0; i < TAG_SPAN && x + i < GRID_W; i++)
            if (grid[y + r][x + i]) return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------ tags + trails */

/* Cell k (0 = next to the tag) of a drawn leader line: back along its direction from the tag. */
static void lead_cell(const struct track *t, u16 k, s16 *x, s16 *y)
{
    u8 d = LEAD_DIR(t->lead);
    *x = t->tag_x - ltag[d][0] - ldir[d][0] * k;
    *y = t->tag_y - ltag[d][1] - ldir[d][1] * k;
}

/* Clear a tag's cells (and its leader's) from plane A; ungrid: release them in the grid too. */
static void clear_tag(struct track *t, bool ungrid)
{
    if (t->tag_x < 0) return;
    for (s16 r = 0; r < 2; r++) {
        row_clear(t->tag_x, t->tag_y + r, TAG_W);
        if (ungrid) tag_mark(t->tag_x, t->tag_y + r, -1);
    }
    if (t->lead & LEAD_ON) {
        for (u16 k = 0; k < LEAD_LEN(t->lead); k++) {
            s16 x, y;
            lead_cell(t, k, &x, &y);
            row_clear(x, y, 1);
            if (ungrid) grid_mark(x, y, 1, -1);
        }
        t->lead &= ~LEAD_ON;                /* direction and length stay: tried first next time */
    }
    t->tag_x = t->tag_y = -1;
}

static void erase_tag(struct track *t) { clear_tag(t, TRUE); }

#define NO_DOT  (-32768)

/* Trail dot n mirrors trail[n]; dot_x == NO_DOT when not drawn (off disc). */
static void dot_draw(struct track *t, u16 n)
{
    s16 bx, by;
    if (to_block(t->trail_x[n], t->trail_y[n], &bx, &by)) {
        scope_dot(bx - 1, by - 1, TRUE);
        t->dot_x[n] = bx - 1;
        t->dot_y[n] = by - 1;
    } else {
        t->dot_x[n] = NO_DOT;
    }
}

static void dot_erase(struct track *t, u16 n)
{
    if (t->dot_x[n] != NO_DOT) scope_dot(t->dot_x[n], t->dot_y[n], FALSE);
    t->dot_x[n] = NO_DOT;
}

static void erase(struct track *t)
{
    erase_tag(t);
    for (u16 n = 0; n < t->trail_n; n++) dot_erase(t, n);
}

static bool want_tag(u16 i)
{
    if ((s16)i == selected && settings.tag_mode != TAGS_OFF) return TRUE;
    switch (settings.tag_mode) {
    case TAGS_ALL: return !(tracks[i].cur.flags & AR_F_GROUND);   /* ground: select to see */
    case TAGS_SELECTED: return (s16)i == selected;
    default: return FALSE;
    }
}

/* right: the text moves right over the trailing spaces both lines share, so a tag west of its leader meets
 * the line (the cells, and the grid footprint, stay x .. x + TAG_W - 1). */
static void put_tag(struct track *t, s16 x, s16 y, bool right)
{
    u16 attr = TILE_ATTR(t->tag_pal, TRUE, FALSE, FALSE), pad = 0;
    if (right)
        while (pad < TAG_W - 1 && t->tag[0][TAG_W - 1 - pad] == ' ' && t->tag[1][TAG_W - 1 - pad] == ' ') pad++;
    tag_row(x + pad, y, t->tag[0], TAG_W - pad, attr);
    tag_row(x + pad, y + 1, t->tag[1], TAG_W - pad, attr);
    tag_mark(x, y, 1);
    tag_mark(x, y + 1, 1);
    t->tag_x = x;
    t->tag_y = y;
}

/* A leader of len cells from the blip's cell in direction d, and the tag at its end: TRUE if it all fits. */
static bool lead_fits(s16 cx, s16 cy, u16 d, u16 len)
{
    for (u16 k = 1; k <= len; k++) {
        s16 x = cx + ldir[d][0] * k, y = cy + ldir[d][1] * k;
        if (x < 1 || y < 1 || x > 26 || y > 26 || grid[y][x]) return FALSE;
    }
    return grid_free(cx + ldir[d][0] * len + ltag[d][0], cy + ldir[d][1] * len + ltag[d][1]);
}

/* STARS data block: the last direction and length if they still fit, else the first that does, nearest first. */
static bool draw_lead(struct track *t, s16 cx, s16 cy)
{
    u16 d = L_DIRS, len = 0;

    if (LEAD_LEN(t->lead) && lead_fits(cx, cy, LEAD_DIR(t->lead), LEAD_LEN(t->lead))) {
        d = LEAD_DIR(t->lead);
        len = LEAD_LEN(t->lead);
    } else {
        for (u16 l = 1; l <= 2 && d == L_DIRS; l++)
            for (u16 c = 0; c < L_DIRS; c++)
                if (lead_fits(cx, cy, c, l)) {
                    d = c;
                    len = l;
                    break;
                }
    }
    if (d == L_DIRS) return FALSE;
    u16 attr = TILE_ATTR_FULL(t->tag_pal, TRUE, FALSE, FALSE, t_lead + ltile[d]);
    for (u16 k = 1; k <= len; k++) {
        s16 x = cx + ldir[d][0] * k, y = cy + ldir[d][1] * k;
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + (((y << 6) + x) << 1)));
        *(vu16 *)VDP_DATA_PORT = attr;
        grid_mark(x, y, 1, 1);
    }
    put_tag(t, cx + ldir[d][0] * len + ltag[d][0], cy + ldir[d][1] * len + ltag[d][1], ldir[d][0] < 0);
    t->lead = LEAD_ON | len << 3 | d;
    return TRUE;
}

static void draw(struct track *t, u16 i)
{
    s16 bx, by;

    if (!want_tag(i) || !t->vis) return;
    bx = t->bx;
    by = t->by;

    s16 cx = (SCOPE_X + bx) >> 3, cy = (SCOPE_Y + by) >> 3;
    if (themes[settings.theme].leader && draw_lead(t, cx, cy)) return;
    static const s8 cand[4][2] = { { 1, -2 }, { 1, 1 }, { -TAG_W, -2 }, { -TAG_W, 1 } };
    for (u16 c = 0; c < 4; c++) {       /* beside the blip, no line (and the fallback for a crowded STARS scope) */
        s16 x = cx + cand[c][0], y = cy + cand[c][1];
        if (!grid_free(x, y)) continue;
        put_tag(t, x, y, FALSE);
        return;
    }
}

static void free_track(struct track *t)
{
    erase(t);
    t->shown = 0;
    t->trail_n = 0;
    t->used = 0;
    tr_state[t - tracks] = 0;
}

/* ------------------------------------------------------------ static text */

static void put(VDPPlane plane, u16 pal, const char *s, s16 x, s16 y)
{
    VDP_drawTextEx(plane, s, TILE_ATTR(pal, TRUE, FALSE, FALSE), x, y, CPU);
}

/* Erase the drawn ring labels + compass and their grid occupancy. Must run before
 * anything resets label_bits: it's the only record of which cells hold label text
 * (a shorter new label would otherwise leave the old one's digits behind). */
static void clear_labels(void)
{
    for (u16 y = 0; y < GRID_H; y++)
        for (u16 x = 0; x < GRID_W; x++)
            if (label_at(x, y)) {
                row_clear(x, y, 1);
                grid[y][x]--;
            }
    memset(label_bits, 0, sizeof(label_bits));
}

static void draw_labels(void)
{
    static const struct { s8 x, y; const char *s; } compass[] = {
        { 13, 2, "N" }, { 13, 25, "S" }, { 2, 13, "W" }, { 25, 13, "E" },
    };
    u16 range = settings_range_nm();

    clear_labels();
    for (u16 i = 0; i < 4; i++) {
        put(BG_A, PAL_TEXT_LOW, compass[i].s, compass[i].x, compass[i].y);
        label_set((u8)compass[i].x, (u8)compass[i].y);
    }
    for (u16 k = 1; k <= 5; k++) {
        char s[4];
        s16 d = (SCOPE_R * k * 181) / (5 * 256);      /* r * cos 45 */
        s16 x = (SCOPE_X + SCOPE_CX + d) >> 3, y = (SCOPE_Y + SCOPE_CY - d) >> 3;
        u16 n = sprintf(s, "%u", range * k / 5);
        put(BG_A, PAL_TEXT_LOW, s, x, y);
        for (u16 i = 0; i < n; i++) label_set(x + i, y);
    }
    for (u16 y = 0; y < GRID_H; y++)
        for (u16 x = 0; x < GRID_W; x++) grid[y][x] += label_at(x, y);
}

/* ------------------------------------------------------------ notice box */

/* Rows [r0, r1) of the step box_from -> box_cur: the bigger rectangle's cells, the new one drawn, the rest
 * (left behind when shrinking) cleared. */
static void box_rows(u16 r0, u16 r1)
{
    vu16 *data = (vu16 *)VDP_DATA_PORT;
    s16 x, y, w, h, ox, oy, ow, oh;
    box_rect(box_cur, &x, &y, &w, &h);
    box_rect(box_from, &ox, &oy, &ow, &oh);
    s16 ux = w > ow ? x : ox, uy = h > oh ? y : oy, uw = w > ow ? w : ow, uh = h > oh ? h : oh;

    for (u16 r = r0; r < r1 && r < uh; r++) {
        s16 cy = uy + r;
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + (((cy << 6) + ux) << 1)));
        for (s16 cx = ux; cx < ux + uw; cx++) {
            if (!w || cx < x || cx >= x + w || cy < y || cy >= y + h) { *data = wx_word(cx, cy); continue; }
            u16 k = (cy == y ? 0 : cy == y + h - 1 ? 6 : 3) + (cx == x ? 0 : cx == x + w - 1 ? 2 : 1);
            *data = TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, t_frame + k);
        }
    }
}

static bool in_rect(s16 ax, s16 ay, s16 aw, s16 ah, s16 x, s16 y, s16 w, s16 h)
{
    return ax + aw > x && ax < x + w && ay + ah > y && ay < y + h;
}

static bool lead_in(const struct track *t, s16 x, s16 y, s16 w, s16 h)
{
    if (!(t->lead & LEAD_ON)) return FALSE;
    for (u16 k = 0; k < LEAD_LEN(t->lead); k++) {
        s16 lx, ly;
        lead_cell(t, k, &lx, &ly);
        if (in_rect(lx, ly, 1, 1, x, y, w, h)) return TRUE;
    }
    return FALSE;
}

/* Occupy (dv 1) or free (-1) the open box's cells in the tag grid. Occupying moves tags inside it elsewhere. */
static void box_occupy(s16 dv)
{
    s16 x, y, w, h;
    box_rect(BOX_STEPS, &x, &y, &w, &h);
    if (dv > 0)
        for (u16 i = 0; i < TR_MAX; i++) {
            struct track *t = &tracks[i];
            if (t->tag_x >= 0 && (in_rect(t->tag_x, t->tag_y, TAG_W, 2, x, y, w, h) || lead_in(t, x, y, w, h))) {
                erase_tag(t);
                t->dirty = 1;
            }
        }
    for (s16 r = 0; r < h; r++) grid_mark(x, y + r, w, dv);
}

/* Every frame: step the animation, or write the text once it's open. */
static void box_tick(void)
{
    if (box_phase) {                                /* the second half of a step's rows */
        box_rows(4, BOX_ROWS);
        box_phase = 0;
        if (!box_cur) {                             /* shut: give the cells back */
            box_occupy(-1);
            draw_labels();
            req_retag = 1;
        }
        return;
    }
    if (box_cur != box_target) {
        static u8 wait;
        if (wait && !box_fast) { wait--; return; }
        wait = BOX_FRAMES - 1;
        if (!box_cur) box_occupy(1);
        box_from = box_cur;
        box_cur += box_target > box_cur ? 1 : -1;
        if (box_cur == BOX_STEPS) box_text_dirty = 0x0F;
        box_rows(0, 4);
        box_phase = 1;
        return;
    }
    if (box_cur == BOX_STEPS && box_text_dirty) {   /* one text row a frame, opaque font */
        static const u8 row_of[4] = { 1, 2, 3, 5 };
        s16 x, y, w, h;
        u16 r = 0;
        while (!(box_text_dirty & (1 << r))) r++;
        box_text_dirty &= ~(1 << r);
        box_rect(BOX_STEPS, &x, &y, &w, &h);
        vu16 *data = (vu16 *)VDP_DATA_PORT;
        u16 base = TILE_ATTR(box_pal[r], TRUE, FALSE, FALSE) + t_ofont - 32;
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + ((((y + row_of[r]) << 6) + x + 1) << 1)));
        for (u16 i = 0; i < AR_NOTICE_COLS; i++) {
            u8 c = box_text[r][i];
            *data = base + (c >= 32 && c < 32 + FONT_LENGTH ? c : ' ');
        }
    }
}

/* ------------------------------------------------------------ V-Int */

/* Own sprite table (VDP format), DMA'd in one go at the start of V-Blank. */
static u16 spr[MAX_BLIPS + 1][4];
static u16 spr_n = 1;

static void place(struct track *t)
{
    t->vis = to_block(t->cur.x, t->cur.y, &t->bx, &t->by);
    t->spr_y = 128 + SCOPE_Y + t->by - 4;
    t->spr_x = 128 + SCOPE_X + t->bx - 4;
    t->spr_attr = TILE_ATTR_FULL((t->cur.flags & AR_F_EMERG) ? PAL3 : PAL1, TRUE, FALSE, FALSE,
                                 (t->cur.flags & AR_F_STALE) ? t_blip + 4 : t_blip);
}

/* blip brightness by sweep steps since the last update (age >> 5) */
static const u8 lvl_tab[8] = { 0, 1, 1, 2, 2, 2, 3, 3 };

static void sprites(void)
{
    u16 n = 0;
    u8 step = scope_step();
    bool fade = themes[settings.theme].blip_fade;
    u16 *e = spr[0];

    u16 max = popup_on ? 0 : MAX_BLIPS;     /* hidden under the pop-up: emit nothing */
    /* the notice box: blips touching it are skipped (high priority, they'd draw over it). Sprite coordinates:
     * a blip at spr covers spr .. spr + 7; the box covers 128 + its cells * 8 */
    s16 bx, by, bw, bh;
    box_rect(box_cur, &bx, &by, &bw, &bh);
    u16 bx0 = 128 + bx * 8 - 8, bx1 = 128 + (bx + bw) * 8, by0 = 128 + by * 8 - 8, by1 = 128 + (by + bh) * 8;

    for (u16 i = 0; i < TR_MAX && n < max; i++) {
        if (!(tr_state[i] & TS_USED)) continue;
        struct track *t = &tracks[i];
        if (!t->shown || !t->vis) continue;
        if (bw && t->spr_x > bx0 && t->spr_x < bx1 && t->spr_y > by0 && t->spr_y < by1) continue;

        u16 attr = t->spr_attr;
        if (fade && !(t->cur.flags & AR_F_STALE)) attr += lvl_tab[(u8)(step - t->hit_step) >> 5];
        e[0] = t->spr_y;
        e[1] = (SPRITE_SIZE(1, 1) << 8) | ++n;
        e[2] = attr;
        e[3] = t->spr_x;
        e += 4;
        if ((s16)i == selected && n < MAX_BLIPS) {
            e[0] = t->spr_y - 4;
            e[1] = (SPRITE_SIZE(2, 2) << 8) | ++n;
            e[2] = TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, t_box);
            e[3] = t->spr_x - 4;
            e += 4;
        }
    }
    if (!n) {
        spr[0][0] = spr[0][3] = 0;      /* off-screen */
        spr[0][2] = 0;
        n = 1;
    }
    spr[n - 1][1] &= 0xFF00;            /* end of link chain */
    spr_n = n;                          /* uploaded at the start of the next V-Blank */
}

static void panel(void)
{
    u16 done = 0;

    u32 bit = 1;
    for (u16 r = 0; r < PANEL_ROWS && done < PANEL_PER_FRAME; r++, bit <<= 1) {
        if (!(panel_dirty & bit)) continue;
        panel_dirty &= ~bit;
        row_write(VDP_WINDOW, PANEL_COL, r, panel_text[r], PANEL_W,
                  TILE_ATTR(panel_pal[r], TRUE, FALSE, FALSE));
        done++;
    }
}

static void popup_rows(void)
{
    vu16 *data = (vu16 *)VDP_DATA_PORT;
    u16 done = 0;
    u32 bit = 1;

    for (u16 r = 0; r < PANEL_ROWS && done < POPUP_PER_FRAME; r++, bit <<= 1) {
        if (!(popup_dirty & bit)) continue;
        popup_dirty &= ~bit;
        u16 base = TILE_ATTR(popup_pal[r], TRUE, FALSE, FALSE) + t_ofont - 32;
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_WINDOW + ((r << 6) << 1)));
        for (u16 x = 0; x < POPUP_COLS; x++) {
            u8 c = popup_text[r][x];
            *data = base + (c >= 32 && c < 32 + FONT_LENGTH ? c : ' ');
        }
        done++;
    }
}

static void radar_vint(void)
{
    /* A late handler (previous one overran) is no longer in V-Blank, where DMA
     * crawls at ~18 B/line and stalls the CPU: that would keep it late forever.
     * Then skip uploads and optional work this frame and just keep the sweep
     * turning; the dirty tiles go out next frame. */
    bool in_vblank = GET_VDP_STATUS(VDP_VBLANK_FLAG);

    if (!in_vblank) {
        late_frames++;
        scope_advance();
        if (scope_step() == 0) rotations++;
        return;
    }
    /* ---- V-Blank: DMA only ---- */
#ifdef RADAR_DEBUG
    u16 prof_l = line_now();
#endif
    scope_upload();
    PROF(0);
    DMA_doDma(DMA_VRAM, spr, VDP_SPRITE_TABLE, spr_n * 4, 2);
    PROF(1);
    if (popup_want != popup_on && !(popup_want && popup_dirty)) {
        popup_on = popup_want;
        VDP_setWindowVPos(FALSE, popup_on ? PANEL_ROWS : 0);   /* all rows: the window covers the scope */
    }

    /* ---- CPU work for the next frame ---- */
    chime_tick();
    JOY_update();
    joy = JOY_readJoypad(JOY_1);
    if (req_theme) {
        req_theme = 0;
        scope_set_theme(&themes[settings.theme]);
    }

    PROF(2);
    scope_advance();
    PROF(3);
    u8 step = scope_step();
    if (step == 0) rotations++;

    if (req_rescale) {
        req_rescale = 0;
        k_scale = geo_scale(settings_range_nm());
        for (u16 i = 0; i < TR_MAX; i++) {
            /* forget drawn dots (cleared in bulk below) and tags */
            for (u16 k = 0; k < TRAIL_N; k++) tracks[i].dot_x[k] = NO_DOT;
            clear_tag(&tracks[i], FALSE);       /* the grid is zeroed below */
            tracks[i].dirty = 2;
            place(&tracks[i]);
        }
        scope_clear_dots();
        clear_labels();
        memset(grid, 0, sizeof(grid));
        wx_dirty = (1UL << AR_WX_N) - 1;        /* radar_rescale wiped the cells (old range) */
        map_next = 0;                           /* scope_clear_dots took the map: draw what's installed now */
        draw_labels();
        if (box_cur) {                          /* the labels went over the notice box: draw it again */
            box_occupy(1);
            box_from = box_cur;
            box_rows(0, BOX_ROWS);
            if (box_cur == BOX_STEPS) box_text_dirty = 0x0F;
        }
    }
    if (req_mapclear) {                         /* MAP off: the fixed pixels go, the trail dots come back */
        req_mapclear = 0;
        scope_clear_dots();
        for (u16 i = 0; i < TR_MAX; i++) {
            for (u16 k = 0; k < TRAIL_N; k++) tracks[i].dot_x[k] = NO_DOT;
            if (tracks[i].dirty < 2) tracks[i].dirty = 2;
        }
    }
    if (req_retag) {
        req_retag = 0;
        for (u16 i = 0; i < TR_MAX; i++) tracks[i].dirty = 1;
    }

    /* updates follow the sweep: apply each aircraft's new data as it is crossed */
    for (u16 i = 0; i < TR_MAX; i++) {
        u8 st = tr_state[i];
        if (!st) continue;
        struct track *t = &tracks[i];
        if (st & TS_DROP) {
            free_track(t);
            if ((s16)i == selected) selected = -1;
            continue;
        }
        if (!(st & TS_NEXT) || tr_brg[i] != step) continue;
        if (t->shown) {
            /* incremental trail: drop the oldest dot, shift, dot at the old position */
            if (t->trail_n == TRAIL_N) dot_erase(t, TRAIL_N - 1);
            for (u16 n = TRAIL_N - 1; n > 0; n--) {
                t->trail_x[n] = t->trail_x[n - 1];
                t->trail_y[n] = t->trail_y[n - 1];
                t->dot_x[n] = t->dot_x[n - 1];
                t->dot_y[n] = t->dot_y[n - 1];
            }
            t->trail_x[0] = t->cur.x;
            t->trail_y[0] = t->cur.y;
            if (t->trail_n < TRAIL_N) t->trail_n++;
            dot_draw(t, 0);
        }
        t->cur = t->next;
        memcpy(t->tag, t->next_tag, sizeof(t->tag));
        t->tag_pal = t->next_pal;
        place(t);
        t->has_next = 0;
        tr_state[i] = st & ~TS_NEXT;
        t->shown = 1;
        t->hit_step = step;
        t->dirty = 1;
    }

    PROF(4);
    /* bounded tag/trail redraws, round robin */
    for (u16 n = 0, scanned = 0; n < REDRAW_PER_FRAME && scanned < TR_MAX; scanned++) {
        struct track *t = &tracks[redraw_cursor];
        u16 i = redraw_cursor;
        if (++redraw_cursor == TR_MAX) redraw_cursor = 0;
        u8 why = t->dirty;
        if (!why) continue;
        t->dirty = 0;
        if (!t->used || !t->shown) continue;
        if (why == 2)                            /* after a rescale: dots too */
            for (u16 k = 0; k < t->trail_n; k++) dot_draw(t, k);
        erase_tag(t);
        draw(t, i);
        n++;
    }

    PROF(5);
    sprites();
    PROF(6);
    panel();
    popup_rows();
    box_tick();
    wx_rows();
    for (u16 px = 0; px < MAP_PX_PER_FRAME && map_next < map_n; map_next++) {
        const struct ar_seg *s = &map_segs[map_next];
        enum map_style st = map_next < map_runways ? MAP_THICK : map_next < map_centres ? MAP_DASHED : MAP_PLAIN;
        px += scope_map_line(s->x1, s->y1, s->x2, s->y2, st);
    }
    PROF(7);

    /* scanlines spent in this handler: V-Int fires at line 224; the V counter
     * reads 224..255 in V-Blank, then 0.. in the next active display */
    u16 v = GET_VCOUNTER;
    u16 used = v >= 224 ? v - 224 : v + 38;
    if (used > vint_lines_max) vint_lines_max = used;
    vint_lines_sum += used;
}

/* ------------------------------------------------------------ setup + API */

/* The font with its background (index 0) made C_OPAQUE: read back from VRAM, so it is whatever
 * font SGDK loaded. Main loop, before radar_start(). */
static void opaque_font(u16 dst)
{
    u32 t[8];

    for (u16 i = 0; i < FONT_LENGTH; i++) {
        *(vu32 *)VDP_CTRL_PORT = VDP_READ_VRAM_ADDR((u32)(TILE_FONT_INDEX + i) * 32);
        for (u16 k = 0; k < 8; k++) {
            u32 v = *(vu32 *)VDP_DATA_PORT, o = 0;
            for (u16 n = 0; n < 32; n += 4) {
                u32 px = (v >> n) & 0xF;
                o |= (px ? px : C_OPAQUE) << n;
            }
            t[k] = o;
        }
        VDP_loadTileData(t, dst + i, 1, CPU);
    }
}

void radar_init(void)
{
    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setVerticalScroll(BG_A, 0);         /* cockpit mode's page flip can leave plane A at 128 */
    VDP_setVerticalScroll(BG_B, 0);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_clearPlane(WINDOW, TRUE);
    VDP_setWindowHPos(TRUE, PANEL_COL / 2);
    VDP_setWindowVPos(FALSE, 0);
    VDP_setBackgroundColor(0);

    scope_init(&themes[settings.theme], TILE_USER_INDEX);
    t_blip = scope_vram_end();
    t_box = t_blip + 5;
    VDP_loadTileData((const u32 *)blip_tiles, t_blip, 5, DMA);
    VDP_loadTileData((const u32 *)box_tiles, t_box, 4, DMA);
    t_ofont = t_box + 4;
    opaque_font(t_ofont);
    t_frame = t_ofont + FONT_LENGTH;
    VDP_loadTileData((const u32 *)frame_tiles, t_frame, 9, DMA);
    t_lead = t_frame + 9;
    VDP_loadTileData((const u32 *)lead_tiles, t_lead, 4, DMA);
    t_wx = t_lead + 4;
    VDP_loadTileData((const u32 *)radar_wx_tiles, t_wx, 3, DMA);

    memset(tracks, 0, TR_MAX * sizeof(struct track));
    memset((void *)tr_state, 0, sizeof(tr_state));
    for (u16 i = 0; i < TR_MAX; i++) tracks[i].tag_x = tracks[i].tag_y = -1;
    memset(grid, 0, sizeof(grid));
    memset(label_bits, 0, sizeof(label_bits));
    memset(wx_cells, 0, sizeof(wx_cells));
    wx_dirty = 0;
    map_n = map_next = 0;
    req_mapclear = 0;
    memset(panel_text, 0, sizeof(panel_text));
    memset(panel_pal, 0, sizeof(panel_pal));
    panel_dirty = 0;
    memset(popup_text, ' ', sizeof(popup_text));
    popup_dirty = 0;
    popup_want = popup_on = 0;
    box_target = box_cur = box_from = box_phase = box_fast = box_text_dirty = 0;
    selected = -1;
    rotations = 0;
    redraw_cursor = 0;
    k_scale = geo_scale(settings_range_nm());
    draw_labels();
    req_rescale = req_theme = req_retag = 0;
}

void radar_start(void)
{
    SYS_setVIntCallback(radar_vint);
}

void radar_stop(void)
{
    SYS_setVIntCallback(NULL);
    VDP_setAutoInc(2);
}

void radar_panel_pal(u16 row, const char *text, u16 pal)
{
    char buf[PANEL_W + 1];

    if (row >= PANEL_ROWS) return;
    /* SGDK's strncpy doesn't zero-pad: clear first so shorter text erases longer */
    memset(buf, 0, sizeof(buf));
    for (u16 i = 0; i < PANEL_W && text[i]; i++) buf[i] = text[i];
    if (pal == panel_pal[row] && !memcmp(buf, panel_text[row], PANEL_W)) return;   /* unchanged */
    SYS_disableInts();
    memcpy(panel_text[row], buf, sizeof(buf));
    panel_pal[row] = pal;
    panel_dirty |= 1UL << row;
    SYS_enableInts();
}

void radar_panel(u16 row, const char *text) { radar_panel_pal(row, text, PAL0); }

void radar_popup_row(u16 row, const char *text, u16 pal)
{
    char buf[POPUP_COLS];
    u16 i = 0;

    if (row >= PANEL_ROWS) return;
    for (; i < POPUP_COLS && text[i]; i++) buf[i] = text[i];
    for (; i < POPUP_COLS; i++) buf[i] = ' ';
    if (pal == popup_pal[row] && !memcmp(buf, popup_text[row], POPUP_COLS)) return;
    SYS_disableInts();
    memcpy(popup_text[row], buf, POPUP_COLS);
    popup_pal[row] = pal;
    popup_dirty |= 1UL << row;
    SYS_enableInts();
}

void radar_popup(bool on)
{
    /* opening: write every row, blank ones too, or the window shows through where it never was */
    if (on && !popup_want) popup_dirty = (1UL << PANEL_ROWS) - 1;
    popup_want = on;
}

/* Main loop: the weather and the map were sampled at the old range; they go now (not in the V-Int), so a
 * fetch right after the zoom can't be wiped by a rescale still pending. */
void radar_rescale(void)
{
    SYS_disableInts();
    req_rescale = 1;
    memset(wx_cells, 0, sizeof(wx_cells));
    wx_dirty = (1UL << AR_WX_N) - 1;
    map_n = map_next = 0;
    SYS_enableInts();
}
void radar_retheme(void) { req_theme = 1; req_retag = 1; }
void radar_retag(void) { req_retag = 1; }

void radar_select(s16 track) { selected = track; }
s16 radar_selected(void) { return selected; }
u16 radar_rotations(void) { return rotations; }
u16 radar_joy(void) { return joy; }

void radar_box_open(const char *text, const u8 pal[4])
{
    SYS_disableInts();
    memcpy(box_text, text, sizeof(box_text));
    memcpy(box_pal, pal, sizeof(box_pal));
    box_text_dirty = 0x0F;
    box_fast = 0;
    box_target = BOX_STEPS;
    SYS_enableInts();
}

void radar_box_pal(u16 row, u16 pal)
{
    if (row >= 4 || box_pal[row] == pal) return;
    SYS_disableInts();
    box_pal[row] = pal;
    box_text_dirty |= 1 << row;
    SYS_enableInts();
}

void radar_box_close(bool now)
{
    SYS_disableInts();
    box_target = 0;
    box_fast = now;
    SYS_enableInts();
}

bool radar_box_up(void) { return box_cur || box_target; }

u8 *radar_map_begin(u16 *size)
{
    SYS_disableInts();
    if (map_next) req_mapclear = 1;         /* lines of the old map are drawn: they go */
    map_n = map_next = 0;                   /* the V-Int leaves the buffer alone until radar_map_end */
    SYS_enableInts();
    *size = sizeof(map_buf);
    return (u8 *)map_buf;
}

void radar_map_end(bool ok)
{
    const struct ar_map *m = (const struct ar_map *)map_buf;
    u16 n = ok ? m->runways + m->centres + m->ticks : 0;

    if (n > AR_MAP_MAX) n = AR_MAP_MAX;
    SYS_disableInts();
    map_runways = ok ? m->runways : 0;
    map_centres = map_runways + (ok ? m->centres : 0);
    map_next = 0;
    map_n = n;
    SYS_enableInts();
}

void radar_wx(const u8 *cells)
{
    SYS_disableInts();
    if (cells) memcpy(wx_cells, cells, sizeof(wx_cells));
    else memset(wx_cells, 0, sizeof(wx_cells));
    wx_dirty = (1UL << AR_WX_N) - 1;
    SYS_enableInts();
}
