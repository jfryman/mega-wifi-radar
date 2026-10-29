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
#define C_OPAQUE         14      /* black in every palette of every theme: the pop-up's background */

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

static u16 t_blip, t_box, t_ofont;
static u16 k_scale;
static u8 grid[GRID_H][GRID_W];         /* tag/label occupancy per plane-A cell */
static u8 label_cells[GRID_H][GRID_W];  /* permanent: ring labels + compass */
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

static void row_clear(u16 plane_addr, u16 x, u16 y, u16 n)
{
    vu16 *data = (vu16 *)VDP_DATA_PORT;

    *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(plane_addr + ((((y) << 6) + (x)) << 1)));
    while (n--) *data = 0;
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

/* A tag's span includes a gap column on its right. A label directly left of it would
 * also read as part of the tag ("S" + "017" looks like "5017"), so check that cell too. */
static bool grid_free(s16 x, s16 y)
{
    if (x < 1 || y < 1 || x + TAG_W > 27 || y + 2 > 27) return FALSE;
    for (s16 r = 0; r < 2; r++) {
        if (label_cells[y + r][x - 1]) return FALSE;
        for (s16 i = 0; i < TAG_SPAN && x + i < GRID_W; i++)
            if (grid[y + r][x + i]) return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------ tags + trails */

static void erase_tag(struct track *t)
{
    if (t->tag_x >= 0) {
        for (s16 r = 0; r < 2; r++) {
            row_clear(VDP_BG_A, t->tag_x, t->tag_y + r, TAG_W);
            grid_mark(t->tag_x, t->tag_y + r, TAG_SPAN, -1);
        }
        t->tag_x = t->tag_y = -1;
    }
}

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

static void draw(struct track *t, u16 i)
{
    s16 bx, by;

    if (!want_tag(i) || !t->vis) return;
    bx = t->bx;
    by = t->by;

    s16 cx = (SCOPE_X + bx) >> 3, cy = (SCOPE_Y + by) >> 3;
    static const s8 cand[4][2] = { { 1, -2 }, { 1, 1 }, { -TAG_W, -2 }, { -TAG_W, 1 } };
    for (u16 c = 0; c < 4; c++) {
        s16 x = cx + cand[c][0], y = cy + cand[c][1];
        if (!grid_free(x, y)) continue;

        u16 attr = TILE_ATTR(t->tag_pal, TRUE, FALSE, FALSE);
        row_write(VDP_BG_A, x, y, t->tag[0], TAG_W, attr);
        row_write(VDP_BG_A, x, y + 1, t->tag[1], TAG_W, attr);
        grid_mark(x, y, TAG_SPAN, 1);
        grid_mark(x, y + 1, TAG_SPAN, 1);
        t->tag_x = x;
        t->tag_y = y;
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
 * anything resets label_cells: it's the only record of which cells hold label text
 * (a shorter new label would otherwise leave the old one's digits behind). */
static void clear_labels(void)
{
    for (u16 y = 0; y < GRID_H; y++)
        for (u16 x = 0; x < GRID_W; x++)
            if (label_cells[y][x]) {
                VDP_clearTextEx(BG_A, 0, x, y, 1, CPU);
                grid[y][x] -= label_cells[y][x];
                label_cells[y][x] = 0;
            }
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
        label_cells[(u8)compass[i].y][(u8)compass[i].x] = 1;
    }
    for (u16 k = 1; k <= 5; k++) {
        char s[4];
        s16 d = (SCOPE_R * k * 181) / (5 * 256);      /* r * cos 45 */
        s16 x = (SCOPE_X + SCOPE_CX + d) >> 3, y = (SCOPE_Y + SCOPE_CY - d) >> 3;
        u16 n = sprintf(s, "%u", range * k / 5);
        put(BG_A, PAL_TEXT_LOW, s, x, y);
        for (u16 i = 0; i < n; i++) label_cells[y][x + i] = 1;
    }
    for (u16 y = 0; y < GRID_H; y++)
        for (u16 x = 0; x < GRID_W; x++) grid[y][x] += label_cells[y][x];
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

    for (u16 i = 0; i < TR_MAX && n < max; i++) {
        if (!(tr_state[i] & TS_USED)) continue;
        struct track *t = &tracks[i];
        if (!t->shown || !t->vis) continue;

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
            if (tracks[i].tag_x >= 0) {
                for (s16 r = 0; r < 2; r++) row_clear(VDP_BG_A, tracks[i].tag_x, tracks[i].tag_y + r, TAG_W);
                tracks[i].tag_x = tracks[i].tag_y = -1;
            }
            tracks[i].dirty = 2;
            place(&tracks[i]);
        }
        scope_clear_dots();
        clear_labels();
        memset(grid, 0, sizeof(grid));
        draw_labels();
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

    memset(tracks, 0, sizeof(tracks));
    memset((void *)tr_state, 0, sizeof(tr_state));
    for (u16 i = 0; i < TR_MAX; i++) tracks[i].tag_x = tracks[i].tag_y = -1;
    memset(grid, 0, sizeof(grid));
    memset(label_cells, 0, sizeof(label_cells));
    memset(panel_text, 0, sizeof(panel_text));
    memset(panel_pal, 0, sizeof(panel_pal));
    panel_dirty = 0;
    memset(popup_text, ' ', sizeof(popup_text));
    popup_dirty = 0;
    popup_want = popup_on = 0;
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
bool radar_popup_shown(void) { return popup_want; }

void radar_rescale(void) { req_rescale = 1; }
void radar_retheme(void) { req_theme = 1; req_retag = 1; }
void radar_retag(void) { req_retag = 1; }

void radar_select(s16 track) { selected = track; }
s16 radar_selected(void) { return selected; }
u16 radar_rotations(void) { return rotations; }
u16 radar_joy(void) { return joy; }
