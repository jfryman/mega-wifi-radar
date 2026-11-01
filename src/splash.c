/* Boot splash (lode/ui/boot-splash.md). Window (rows 0-7): the wordmark and the status line.
 * Plane B: the scene, with the speech bubble's body written over it. Plane A: only the bubble
 * text. Sprites (low priority): the 787, so it climbs out *behind* the wordmark. Everything
 * moves in the V-Int; main only requests phases. */
#include "splash.h"
#include "airports.h"
#include "resources.h"
#include "settings.h"
#include "splash_data.h"
#include "sfx.h"

#define WIN_ROWS    8           /* window plane rows 0..7: wordmark + status, never scrolled */
#define LOGO_X      2           /* wordmark cell position (36x6 cells) */
#define LOGO_Y      1
#define STATUS_ROW  7
#define STATUS_COL  1
#define STATUS_W    38

#define BUB_TW      26          /* bubble text: columns per line, lines */
#define BUB_LINES   4
#define TOWER_COL   8           /* tower bubble: left edge, bottom row (tail at the cab windows) */
#define TOWER_BOT   14
#define PLANE_BOT   20          /* 787 bubble: bottom row, tail tip on the row below */
#define NOSE_DX     86          /* sprite x -> cockpit x */

#define GATE_X      (20 << 8)   /* sprite x, 8.8 fixed point */
#define HOLD_X      (103 << 8)  /* nose just short of the hold bars at x = 200 */
#define GROUND_Y    (161 << 8)  /* wheels on the runway centre line */
#define DWELL       90          /* frames a call stays up before the next phase */
#define LINGER      90          /* ...and at least this long after its last letter, to read it */
#define TYPE_RATE   1           /* frames per typed character */

enum { PAL_CALL = PAL1, PAL_RED = PAL2, PAL_BUB = PAL3, PAL_DIM = PAL3 };

/* internal phases after the takeoff clearance */
enum { SP_READBACK = SP_NO_MODULE + 1, SP_ROLL, SP_CONTACT, SP_GONE };

/* who is talking: where the bubble opens and where its tail points */
enum { NOBODY, TOWER, PLANE };

/* bubble tiles, in tools/splashgen's bubbleTiles order */
enum { BT_TL, BT_T, BT_TR, BT_L, BT_FILL, BT_R, BT_BL, BT_B, BT_BR, BT_LGAP, BT_LTIP, BT_BGAP, BT_BTIP };

static u16 t_bg, t_logo, t_plane, t_bub;
static char city[10];

static volatile u16 phase, phase_frames, typed, typed_end, typed_at, skip, fade;   /* typed_at: frame typing ended */
static char lines[BUB_LINES][BUB_TW];
static u16 line_pal[BUB_LINES], line_len[BUB_LINES], nlines;
static u16 bub_x, bub_y, bub_w, bub_h;          /* bubble body, cells (0 wide: none) */
static u16 clr_x, clr_y, clr_w, clr_h;          /* body + tail: what to give back to the scene */
static char status[STATUS_W + 1];
static volatile u16 status_dirty;


static s32 px, py, vx, vy;              /* 8.8 */
static u16 frame_no;
static u16 spr[6][4];
static u16 pal_now[64];
static u16 ticks;
static u16 talker;                      /* who the current bubble belongs to */
static u16 rpm;                         /* engines, 0..255 (lode/ui/boot-sound.md) */

/* ------------------------------------------------------------ VDP helpers */

static void row_fill(u16 plane_addr, u16 x, u16 y, u16 n, u16 word)
{
    *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(plane_addr + (((y << 6) + x) << 1)));
    while (n--) *(vu16 *)VDP_DATA_PORT = word;
}

static void map_write(u16 plane_addr, u16 x, u16 y, u16 w, u16 h, const u16 *map, u16 base, bool zero_blank)
{
    for (u16 r = 0; r < h; r++) {
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(plane_addr + ((((y + r) << 6) + x) << 1)));
        for (u16 c = 0; c < w; c++) {
            u16 t = *map++;
            *(vu16 *)VDP_DATA_PORT = (zero_blank && !t) ? 0 : base + t;
        }
    }
}

static inline u16 text_word(u16 pal, char c)
{
    return TILE_ATTR_FULL(pal, TRUE, FALSE, FALSE, TILE_FONT_INDEX + c - 32);
}

/* ------------------------------------------------------------ bubble */

/* Word-wrap text into the bubble from a new line (BUB_TW columns, BUB_LINES lines; the rest is cut). */
static void say(u16 pal, const char *s)
{
    u16 n = nlines, len = 0;

    while (*s && n < BUB_LINES) {
        while (*s == ' ') s++;
        const char *e = s;
        while (*e && *e != ' ') e++;
        u16 wl = e - s;
        if (!wl) break;
        if (wl > BUB_TW) wl = BUB_TW;
        if (len && len + 1 + wl > BUB_TW) {
            line_pal[n] = pal;
            line_len[n++] = len;
            len = 0;
            if (n == BUB_LINES) break;
        }
        if (len) lines[n][len++] = ' ';
        for (u16 i = 0; i < wl; i++) lines[n][len++] = s[i];
        s = e;
    }
    if (len) { line_pal[n] = pal; line_len[n++] = len; }
    nlines = n;
}

/* Calls for a phase (city = first word of the home airport's name). Returns the speaker. */
static u16 script(u16 p, const char *detail)
{
    char buf[96];           /* the longest call plus a 9-char city */

    nlines = 0;
    switch (p) {
    case SP_GATE:
    case SP_NO_MODULE:
        if (p == SP_GATE) {
            sprintf(buf, "%s GROUND, MEGA 787, AT GATE, RADIO CHECK.", city);
            say(PAL_CALL, buf);
        } else {
            sprintf(buf, "%s GROUND, MEGA 787, AT GATE.", city);
            say(PAL_CALL, buf);
            say(PAL_RED, "NO REPLY:");
            say(PAL_RED, detail);
        }
        return PLANE;
    case SP_TAXI:
        sprintf(buf, "MEGA 787, %s GROUND, FIVE BY FIVE. TAXI RUNWAY 9 VIA ALPHA.", city);
        say(PAL_CALL, buf);
        return TOWER;
    case SP_HOLD:
        say(PAL_CALL, "MEGA 787, HOLD SHORT RUNWAY 9, CONTACT TOWER 118.7.");
        return TOWER;
    case SP_RELAY:
        say(PAL_CALL, "TOWER, MEGA 787, HOLDING SHORT, REQUEST RADAR SERVICE.");
        return PLANE;
    case SP_HOLD_POS:
        say(PAL_CALL, "MEGA 787, HOLD POSITION.");
        say(PAL_RED, detail);
        return TOWER;
    case SP_TAKEOFF:
        sprintf(buf, "MEGA 787, %s TOWER, RUNWAY 9, WIND 090 AT 8, CLEARED FOR TAKEOFF.", city);
        say(PAL_CALL, buf);
        return TOWER;
    case SP_READBACK:
        say(PAL_CALL, "CLEARED FOR TAKEOFF RUNWAY 9, MEGA 787.");
        return PLANE;
    case SP_CONTACT:
        say(PAL_CALL, "MEGA 787, RADAR CONTACT. GOOD DAY.");
        return TOWER;
    }
    return NOBODY;
}

static inline u16 bub_word(u16 t)
{
    return TILE_ATTR_FULL(PAL_BUB, TRUE, FALSE, FALSE, t_bub + t);
}

static void bub_tile(u16 x, u16 y, u16 t)
{
    *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_B + (((y << 6) + x) << 1)));
    *(vu16 *)VDP_DATA_PORT = bub_word(t);
}

/* Give the last bubble's cells back: scene on plane B, nothing on plane A. */
static void bubble_close(void)
{
    const u16 *map = (const u16 *)splash_bg_map;
    u16 base = TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, t_bg);

    for (u16 r = clr_y; r < clr_y + clr_h; r++) {
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_B + (((r << 6) + clr_x) << 1)));
        for (u16 c = clr_x; c < clr_x + clr_w; c++) *(vu16 *)VDP_DATA_PORT = base + map[r * 40 + c];
        row_fill(VDP_BG_A, clr_x, r, clr_w, 0);
    }
    clr_w = bub_w = 0;
}

/* Open a bubble around lines[] with its tail at the speaker. */
static void bubble_open(u16 who)
{
    u16 w = 0, tail = 0;

    for (u16 n = 0; n < nlines; n++) if (line_len[n] > w) w = line_len[n];
    bub_w = w + 2;
    bub_h = nlines + 2;
    if (who == TOWER) {
        bub_x = TOWER_COL;
        bub_y = TOWER_BOT + 1 - bub_h;
        clr_x = bub_x - 1; clr_y = bub_y; clr_w = bub_w + 1; clr_h = bub_h;
    } else {
        tail = ((px >> 8) + NOSE_DX) >> 3;
        s16 x = (s16)tail - (s16)(bub_w >> 1);
        if (x < 1) x = 1;
        if (x > 39 - (s16)bub_w) x = 39 - bub_w;
        bub_x = x;
        bub_y = PLANE_BOT + 1 - bub_h;
        if (tail < bub_x + 1) tail = bub_x + 1;
        if (tail > bub_x + bub_w - 2) tail = bub_x + bub_w - 2;
        clr_x = bub_x; clr_y = bub_y; clr_w = bub_w; clr_h = bub_h + 1;
    }

    for (u16 r = 0; r < bub_h; r++) {
        u16 row = r == 0 ? BT_TL : r == bub_h - 1 ? BT_BL : BT_L;   /* left, middle, right = +0, +1, +2 */
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_B + ((((bub_y + r) << 6) + bub_x) << 1)));
        for (u16 c = 0; c < bub_w; c++)
            *(vu16 *)VDP_DATA_PORT = bub_word(row + (c == 0 ? 0 : c == bub_w - 1 ? 2 : 1));
    }
    if (who == TOWER) {
        bub_tile(bub_x, TOWER_BOT - 1, BT_LGAP);
        bub_tile(bub_x - 1, TOWER_BOT - 1, BT_LTIP);
    } else {
        bub_tile(tail, PLANE_BOT, BT_BGAP);
        bub_tile(tail, PLANE_BOT + 1, BT_BTIP);
    }
}

/* Switch phase: new call, new bubble. Called from the V-Int, or via set_phase(). */
static void switch_phase(u16 p, const char *detail)
{
    u16 who = script(p, detail);
    phase = p;
    phase_frames = 0;
    typed = 0;
    typed_end = nlines * BUB_TW;
    typed_at = 0;
    talker = who;
    bubble_close();
    if (who != NOBODY) {
        bubble_open(who);
        sfx_squelch(6, 3);              /* squelch opens the call */
    }
}

/* From main: the V-Int also writes the VDP, so keep it out while we do. */
static void set_phase(u16 p, const char *detail)
{
    SYS_disableInts();
    switch_phase(p, detail);
    SYS_enableInts();
}

/* ------------------------------------------------------------ motion */

/* The call is fully typed, up >= min frames, and has stayed up >= linger frames since. */
static bool call_done(u16 min, u16 linger)
{
    return typed >= typed_end && phase_frames >= min && (u16)(phase_frames - typed_at) >= linger;
}

static void move(void)
{
    switch (phase) {
    case SP_GATE:
    case SP_NO_MODULE:
    case SP_HOLD_POS:
        vx = 0;
        break;
    case SP_TAXI:
    case SP_HOLD:
    case SP_RELAY:
    case SP_TAKEOFF:
    case SP_READBACK:                           /* taxi on to the hold line and wait there */
        vx = px < HOLD_X ? 128 : 0;
        if (px > HOLD_X) px = HOLD_X;
        if (phase == SP_TAKEOFF && call_done(0, LINGER)) switch_phase(SP_READBACK, NULL);
        else if (phase == SP_READBACK && call_done(0, LINGER) && px >= HOLD_X) switch_phase(SP_ROLL, NULL);
        break;
    default:                                    /* takeoff roll and climb */
        if (px < (150 << 8)) {
            if (vx < 3 << 8) vx += 25;          /* roll */
        } else {                                /* climb out steeply so it passes behind the wordmark */
            if (vx > 2 << 8) vx -= 4;
            if (vy > -(vx + (vx >> 1))) vy -= 24;     /* ~56 degrees: the ground is low, the wordmark high */
        }
        break;
    }
    px += vx;
    py += vy;

    s16 x = px >> 8;
    frame_no = x < 135 || phase < SP_ROLL ? 0 : x < 150 ? 1 : x < 170 ? 2 : 3;
    if (phase == SP_ROLL && (py >> 8) < 40) switch_phase(SP_CONTACT, NULL);
    if (phase == SP_CONTACT && (x >= 330 || (py >> 8) < -48) && call_done(0, LINGER)) phase = SP_GONE;
}

static void build_sprites(void)
{
    s16 x = px >> 8, y = py >> 8;
    bool off = x >= 320 || y <= -48;
    u16 base = t_plane + frame_no * SPLASH_PLANE_FRAME_TILES;

    for (u16 i = 0; i < 6; i++) {
        u16 sx = i % 3, sy = i / 3;
        u16 *e = spr[i];
        e[0] = off ? 0 : 128 + y + sy * 24;
        e[1] = (((4 - 1) << 2 | (3 - 1)) << 8) | (i < 5 ? i + 1 : 0);
        e[2] = TILE_ATTR_FULL(PAL2, FALSE, FALSE, FALSE, base + i * 12);
        e[3] = off ? 0 : 128 + x + sx * 32;
    }
}

/* ------------------------------------------------------------ palette effects */

static u16 scale(u16 c, u16 k)          /* k = 0..8 */
{
    u16 b = (c >> 9 & 7) * k >> 3, g = (c >> 5 & 7) * k >> 3, r = (c >> 1 & 7) * k >> 3;
    return b << 9 | g << 5 | r << 1;
}

static void palette_tick(void)
{
    const u16 *pal = (const u16 *)splash_pal;
    static const u16 blue = 0xC42, light = 0xE86, glint = 0xECA;   /* soft glint, not a white blob */

    ticks++;
    for (u16 i = 0; i < 64; i++) pal_now[i] = pal[i];
    /* beacon + edge lights blink */
    if ((ticks >> 5) & 1) pal_now[11] = 0x002;
    /* the glint: one slot white, its neighbours light, every 3 s */
    s16 pos = (s16)((ticks % 180) >> 2) - 4;
    for (s16 s = 0; s < SPLASH_SHINE_SLOTS; s++) {
        s16 d = s - pos;
        pal_now[16 + SPLASH_SHINE_FIRST + s] = d == 0 ? glint : (d == 1 || d == -1) ? light : blue;
    }
    /* fade in over the first 16 frames, out when asked */
    u16 k = ticks < 16 ? ticks >> 1 : 8;
    if (fade) k = fade < 16 ? (16 - fade) >> 1 : 0;
    if (k < 8) for (u16 i = 0; i < 64; i++) pal_now[i] = scale(pal_now[i], k);
}

/* ------------------------------------------------------------ sound */

/* Engines: spool to idle, a little more while taxiing, full power from the roll; quieter and lower
 * as the 787 climbs away, ducked under a radio call. */
static void engines(void)
{
    u16 target = phase >= SP_ROLL ? 255 : vx ? 110 : 90;
    u16 step = phase >= SP_ROLL ? 3 : 1;

    if (rpm + step <= target) rpm += step;
    else if (rpm > target) rpm--;
    else rpm = target;

    s16 climb = (s16)((GROUND_Y - py) >> 12);           /* 16 px steps above the runway */
    if (climb < 0) climb = 0;
    if (climb > 15) climb = 15;
    u16 away = climb + (fade >> 1);
    if ((px >> 8) >= 320 || (py >> 8) <= -48) away += 4;
    u16 sag = climb << 2;                               /* pitch drops as it leaves (Doppler-ish) */
    u16 pitch = rpm > sag ? rpm - sag : 0;
    u16 duck = talker != NOBODY && typed < typed_end ? 3 : 0;
    sfx_tick(rpm, pitch, duck, away > 15 ? 15 : away);
}

/* ------------------------------------------------------------ V-Int */

static void splash_vint(void)
{
    /* V-Blank: uploads first */
    DMA_doDma(DMA_VRAM, spr, VDP_SPRITE_TABLE, 6 * 4, 2);
    PAL_setColors(0, pal_now, 64, CPU);
    VDP_setAutoInc(2);

    /* typewriter: redraw the bubble text from (lines, typed), so it's pure state */
    if (typed < typed_end && !(phase_frames % TYPE_RATE)) {
        u16 n = typed / BUB_TW;
        u16 i = typed % BUB_TW;
        sfx_letter(talker == TOWER, i < line_len[n] ? lines[n][i] : ' ');
        typed = i < line_len[n] ? typed + 1 : (n + 1) * BUB_TW;
        if (typed >= typed_end) {
            typed_at = phase_frames;
            sfx_squelch(4, 5);          /* ...and its tail closes it */
        }
    }
    for (u16 n = 0; n < nlines; n++) {
        u16 shown = typed >= (n + 1) * BUB_TW ? line_len[n] : typed > n * BUB_TW ? typed - n * BUB_TW : 0;
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + ((((bub_y + 1 + n) << 6) + bub_x + 1) << 1)));
        for (u16 i = 0; i < line_len[n]; i++)
            *(vu16 *)VDP_DATA_PORT = i < shown && lines[n][i] > ' ' ? text_word(line_pal[n], lines[n][i]) : 0;
    }
    if (status_dirty) {
        status_dirty = 0;
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_WINDOW + (((STATUS_ROW << 6) + STATUS_COL) << 1)));
        for (u16 i = 0; i < STATUS_W; i++) *(vu16 *)VDP_DATA_PORT = status[i] > ' ' ? text_word(PAL_DIM, status[i]) : 0;
    }

    if (JOY_readJoypad(JOY_1) & BUTTON_START) skip = 1;
    phase_frames++;
    if (fade) fade++;
    move();
    if (skip && phase >= SP_TAKEOFF && phase != SP_NO_MODULE && phase < SP_GONE) phase = SP_GONE;
    build_sprites();
    palette_tick();
    engines();
}

/* ------------------------------------------------------------ API */

/* "NASHVILLE" from the home airport's name (settings can change between calls: load, setup). */
static void update_city(void)
{
    s16 a = airport_find(settings.icao);
    const char *name = a >= 0 ? airports[a].name : settings.icao;
    u16 i = 0;
    while (i < sizeof(city) - 1 && name[i] && name[i] != ' ') { city[i] = name[i]; i++; }
    city[i] = 0;
}

void splash_start(enum splash_phase at)
{
    update_city();

    VDP_setScreenWidth320();
    VDP_setPlaneSize(64, 32, TRUE);
    VDP_setWindowHPos(FALSE, 0);
    VDP_setWindowVPos(FALSE, WIN_ROWS);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0);
    VDP_setVerticalScroll(BG_B, 0);
    VDP_setBackgroundColor(0);
    PAL_setColors(0, palette_black, 64, CPU);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_clearPlane(WINDOW, TRUE);

    t_bg = TILE_USER_INDEX;
    t_logo = t_bg + SPLASH_BG_TILES;
    t_plane = t_logo + SPLASH_LOGO_TILES;
    t_bub = t_plane + SPLASH_PLANE_FRAMES * SPLASH_PLANE_FRAME_TILES;
    VDP_loadTileData((const u32 *)splash_bg_tiles, t_bg, SPLASH_BG_TILES, DMA);
    VDP_loadTileData((const u32 *)splash_logo_tiles, t_logo, SPLASH_LOGO_TILES, DMA);
    VDP_loadTileData((const u32 *)splash_plane_tiles, t_plane, SPLASH_PLANE_FRAMES * SPLASH_PLANE_FRAME_TILES, DMA);
    VDP_loadTileData((const u32 *)splash_bubble_tiles, t_bub, SPLASH_BUBBLE_TILES, DMA);
    VDP_setAutoInc(2);
    map_write(VDP_BG_B, 0, 0, 40, 28, (const u16 *)splash_bg_map, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, t_bg), FALSE);
    map_write(VDP_WINDOW, LOGO_X, LOGO_Y, SPLASH_LOGO_W, SPLASH_LOGO_H, (const u16 *)splash_logo_map,
              TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, t_logo), TRUE);

    px = at == SP_HOLD ? HOLD_X : GATE_X;
    py = GROUND_Y;
    vx = vy = 0;
    frame_no = 0;
    skip = fade = 0;
    ticks = 0;
    memset(status, 0, sizeof(status));
    status_dirty = 1;
    nlines = 0;
    clr_w = bub_w = 0;
    rpm = 0;
    talker = NOBODY;
    sfx_on(FALSE);                      /* silent until main knows the SOUND setting */
    set_phase(at, NULL);
    build_sprites();
    palette_tick();
    SYS_setVIntCallback(splash_vint);
}

void splash_wait(void)
{
    while (!skip && !call_done(DWELL, LINGER)) {}
}

void splash_phase(enum splash_phase p, const char *detail)
{
    splash_wait();
    update_city();
    set_phase(p, detail);
}

void splash_status(const char *s)
{
    u16 i = 0;
    SYS_disableInts();
    for (; i < STATUS_W && s[i]; i++) status[i] = s[i];
    for (; i < STATUS_W; i++) status[i] = 0;
    status_dirty = 1;
    SYS_enableInts();
}

void splash_finish(void)
{
    while (phase != SP_GONE) {}
    if (!skip) { u16 t = phase_frames; while ((u16)(phase_frames - t) < 40) {} }
    fade = 1;
    while (fade < 18) {}
    splash_stop();
}

void splash_sound(bool on)
{
    SYS_disableInts();
    rpm = 0;
    sfx_on(on);
    SYS_enableInts();
}

void splash_stop(void)
{
    SYS_setVIntCallback(NULL);
    sfx_on(FALSE);
    VDP_setAutoInc(2);
    PAL_setColors(0, palette_black, 64, CPU);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_clearPlane(WINDOW, TRUE);
    VDP_setWindowVPos(FALSE, 0);
    VDP_resetSprites();
    VDP_updateSprites(1, CPU);
}
