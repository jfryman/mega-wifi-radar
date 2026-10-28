/* Boot splash (lode/ui/boot-splash.md). Plane B: the scene. Plane A (high priority): the
 * wordmark and the radio box. Sprites (low priority): the 787, so it climbs out *behind*
 * the wordmark. Everything moves in the V-Int; main only requests phases. */
#include "splash.h"
#include "airports.h"
#include "resources.h"
#include "settings.h"
#include "splash_data.h"

#define LOGO_X      2           /* wordmark cell position (36x6 cells) */
#define LOGO_Y      8
#define TEXT_ROW    23          /* two radio lines */
#define STATUS_ROW  26
#define TEXT_COL    1
#define TEXT_W      38

#define GATE_X      (20 << 8)   /* sprite x, 8.8 fixed point */
#define HOLD_X      (103 << 8)  /* nose just short of the hold bars at x = 200 */
#define GROUND_Y    (129 << 8)  /* wheels on the runway centre line */
#define DWELL       90          /* frames a call stays up before the next phase */
#define TYPE_RATE   1           /* frames per typed character */

enum { PAL_PILOT = PAL0, PAL_ATC = PAL1, PAL_RED = PAL2, PAL_DIM = PAL3 };

/* internal phases after takeoff */
enum { SP_ROLL = SP_NO_MODULE + 1, SP_CONTACT, SP_GONE };

static u16 t_bg, t_logo, t_plane;
static char city[10];           /* <= 9 chars keeps every call within TEXT_W */

static volatile u16 phase, phase_frames, typed, skip, fade;
static char lines[2][TEXT_W + 1];
static u16 line_pal[2], line_len[2];
static char status[TEXT_W + 1];
static volatile u16 status_dirty;

static s32 px, py, vx, vy;              /* 8.8 */
static u16 frame_no;
static u16 spr[6][4];
static u16 pal_now[64];
static u16 ticks;

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

/* ------------------------------------------------------------ script */

static void say(u16 n, u16 pal, const char *a, const char *b)
{
    char *d = lines[n];
    u16 i = 0;
    while (i < TEXT_W && *a) d[i++] = *a++;
    while (i < TEXT_W && b && *b) d[i++] = *b++;
    line_len[n] = i;
    while (i < TEXT_W) d[i++] = ' ';
    d[TEXT_W] = 0;
    line_pal[n] = pal;
}

/* Calls for a phase (city = first word of the home airport's name). */
static void script(u16 p, const char *detail)
{
    char buf[64];           /* room for any city; say() cuts lines at TEXT_W */

    switch (p) {
    case SP_GATE:
    case SP_NO_MODULE:
        sprintf(buf, "%s GROUND, MEGA 787, AT GATE,", city);
        say(0, PAL_PILOT, buf, NULL);
        if (p == SP_GATE) say(1, PAL_PILOT, "RADIO CHECK.", NULL);
        else say(1, PAL_RED, "NO REPLY: ", detail);
        break;
    case SP_TAXI:
        sprintf(buf, "MEGA 787, %s GROUND, FIVE BY", city);
        say(0, PAL_ATC, buf, NULL);
        say(1, PAL_ATC, "FIVE. TAXI RUNWAY 9 VIA ALPHA.", NULL);
        break;
    case SP_HOLD:
        say(0, PAL_ATC, "MEGA 787, HOLD SHORT RUNWAY 9,", NULL);
        say(1, PAL_ATC, "CONTACT TOWER 118.7.", NULL);
        break;
    case SP_HOLD_POS:
        say(0, PAL_ATC, "MEGA 787, HOLD POSITION.", NULL);
        say(1, PAL_RED, detail, NULL);
        break;
    case SP_TAKEOFF:
        sprintf(buf, "MEGA 787, %s TOWER, RUNWAY 9,", city);
        say(0, PAL_ATC, buf, NULL);
        say(1, PAL_ATC, "WIND 090 AT 8, CLEARED FOR TAKEOFF.", NULL);
        break;
    case SP_ROLL:
        say(0, PAL_PILOT, "CLEARED FOR TAKEOFF RUNWAY 9,", NULL);
        say(1, PAL_PILOT, "MEGA 787.", NULL);
        break;
    case SP_CONTACT:
        say(0, PAL_ATC, "MEGA 787, RADAR CONTACT.", NULL);
        say(1, PAL_ATC, "GOOD DAY.", NULL);
        break;
    }
}

/* Switch phase: new calls, cleared radio box. Called from the V-Int, or via set_phase(). */
static void switch_phase(u16 p, const char *detail)
{
    script(p, detail);
    phase = p;
    phase_frames = 0;
    typed = 0;
    row_fill(VDP_BG_A, TEXT_COL, TEXT_ROW, TEXT_W, 0);
    row_fill(VDP_BG_A, TEXT_COL, TEXT_ROW + 1, TEXT_W, 0);
}

/* From main: the V-Int also writes the VDP, so keep it out while we do. */
static void set_phase(u16 p, const char *detail)
{
    SYS_disableInts();
    switch_phase(p, detail);
    SYS_enableInts();
}

/* ------------------------------------------------------------ motion */

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
        vx = px < HOLD_X ? 128 : 0;
        if (px > HOLD_X) px = HOLD_X;
        break;
    case SP_TAKEOFF:                            /* hold until the clearance has been read */
        if (typed < 2 * TEXT_W || phase_frames < 50) break;
        /* fall through */
    default:                                    /* takeoff roll and climb */
        if (px < (150 << 8)) {
            if (vx < 3 << 8) vx += 25;          /* roll */
        } else {                                /* climb out steeply so it passes behind the wordmark */
            if (vx > 2 << 8) vx -= 4;
            if (vy > -vx) vy -= 24;
        }
        break;
    }
    px += vx;
    py += vy;

    s16 x = px >> 8;
    frame_no = x < 135 || phase < SP_TAKEOFF ? 0 : x < 150 ? 1 : x < 170 ? 2 : 3;
    if (phase == SP_TAKEOFF && x >= 135) switch_phase(SP_ROLL, NULL);
    if (phase == SP_ROLL && (py >> 8) < 40) switch_phase(SP_CONTACT, NULL);
    if (phase == SP_CONTACT && (x >= 330 || (py >> 8) < -48) && typed >= 2 * TEXT_W) phase = SP_GONE;
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

/* ------------------------------------------------------------ V-Int */

static void splash_vint(void)
{
    /* V-Blank: uploads first */
    DMA_doDma(DMA_VRAM, spr, VDP_SPRITE_TABLE, 6 * 4, 2);
    PAL_setColors(0, pal_now, 64, CPU);
    VDP_setAutoInc(2);

    /* typewriter: redraw both radio rows from (lines, typed) so the box is pure state */
    if (typed < 2 * TEXT_W && !(phase_frames % TYPE_RATE)) {
        u16 n = typed / TEXT_W;
        typed = typed % TEXT_W < line_len[n] ? typed + 1 : (n + 1) * TEXT_W;
    }
    for (u16 n = 0; n < 2; n++) {
        u16 shown = typed >= (n + 1) * TEXT_W ? TEXT_W : typed > n * TEXT_W ? typed - n * TEXT_W : 0;
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + ((((TEXT_ROW + n) << 6) + TEXT_COL) << 1)));
        for (u16 i = 0; i < TEXT_W; i++)
            *(vu16 *)VDP_DATA_PORT = i < shown && lines[n][i] > ' ' ? text_word(line_pal[n], lines[n][i]) : 0;
    }
    if (status_dirty) {
        status_dirty = 0;
        *(vu32 *)VDP_CTRL_PORT = VDP_WRITE_VRAM_ADDR((u32)(VDP_BG_A + (((STATUS_ROW << 6) + TEXT_COL) << 1)));
        for (u16 i = 0; i < TEXT_W; i++) *(vu16 *)VDP_DATA_PORT = status[i] > ' ' ? text_word(PAL_DIM, status[i]) : 0;
    }

    if (JOY_readJoypad(JOY_1) & BUTTON_START) skip = 1;
    phase_frames++;
    if (fade) fade++;
    move();
    if (skip && phase >= SP_TAKEOFF && phase < SP_GONE) phase = SP_GONE;
    build_sprites();
    palette_tick();
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
    VDP_setWindowVPos(FALSE, 0);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_A, 0);
    VDP_setVerticalScroll(BG_B, 0);
    VDP_setBackgroundColor(0);
    PAL_setColors(0, palette_black, 64, CPU);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);

    t_bg = TILE_USER_INDEX;
    t_logo = t_bg + SPLASH_BG_TILES;
    t_plane = t_logo + SPLASH_LOGO_TILES;
    VDP_loadTileData((const u32 *)splash_bg_tiles, t_bg, SPLASH_BG_TILES, DMA);
    VDP_loadTileData((const u32 *)splash_logo_tiles, t_logo, SPLASH_LOGO_TILES, DMA);
    VDP_loadTileData((const u32 *)splash_plane_tiles, t_plane, SPLASH_PLANE_FRAMES * SPLASH_PLANE_FRAME_TILES, DMA);
    VDP_setAutoInc(2);
    map_write(VDP_BG_B, 0, 0, 40, 28, (const u16 *)splash_bg_map, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, t_bg), FALSE);
    map_write(VDP_BG_A, LOGO_X, LOGO_Y, SPLASH_LOGO_W, SPLASH_LOGO_H, (const u16 *)splash_logo_map,
              TILE_ATTR_FULL(PAL1, TRUE, FALSE, FALSE, t_logo), TRUE);

    px = at == SP_HOLD ? HOLD_X : GATE_X;
    py = GROUND_Y;
    vx = vy = 0;
    frame_no = 0;
    skip = fade = 0;
    ticks = 0;
    memset(status, 0, sizeof(status));
    status_dirty = 1;
    set_phase(at, NULL);
    build_sprites();
    palette_tick();
    SYS_setVIntCallback(splash_vint);
}

void splash_phase(enum splash_phase p, const char *detail)
{
    while (!skip && (typed < 2 * TEXT_W || phase_frames < DWELL)) {}
    update_city();
    set_phase(p, detail);
}

void splash_status(const char *s)
{
    u16 i = 0;
    SYS_disableInts();
    for (; i < TEXT_W && s[i]; i++) status[i] = s[i];
    for (; i < TEXT_W; i++) status[i] = 0;
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

void splash_stop(void)
{
    SYS_setVIntCallback(NULL);
    VDP_setAutoInc(2);
    PAL_setColors(0, palette_black, 64, CPU);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_resetSprites();
    VDP_updateSprites(1, CPU);
}
