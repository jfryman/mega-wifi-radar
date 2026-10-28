#include "scope.h"
#include "resources.h"

#define CELLS       26
#define NTILES      568             /* must match tools/scopegen output */
#define GLOW_Q      3               /* frames per glow slot */
#define TAIL        ((GLOW_SLOTS - 1) * GLOW_Q)  /* steps; retired before slot reuse */
#define FLUSH_MAX   48              /* dirty tiles uploaded per frame */

_Static_assert(sizeof(scope_tiles_rom) == NTILES * 32, "scope tiles != NTILES (rerun scopegen?)");

static u32 tiles[NTILES * 8];               /* RAM copy of the disc tiles (18 KB) */
static u8 trail_bits[NTILES * 8];           /* 1 bit per pixel: trail dot here */
static u8 trail_cnt[NTILES];                /* trail pixels per tile (fast path when 0) */
static const u8 bitmask[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
static u8 dirty_flag[NTILES];
static u16 dirty_q[NTILES];
static u16 dirty_n;
static u16 cell_tile[CELLS * CELLS];        /* block cell -> disc tile + 1, 0 = none */
static u16 vram_base;
static u16 tick;
static const struct theme *theme;

static inline u8 px_get(const u8 *t, u16 p)
{
    u8 b = t[p >> 1];
    return (p & 1) ? (b & 0x0F) : (b >> 4);
}

static inline void px_set(u16 tile, u16 p, u8 v)
{
    u8 *b = (u8 *)tiles + tile * 32 + (p >> 1);
    *b = (p & 1) ? ((*b & 0xF0) | v) : ((*b & 0x0F) | (v << 4));
}

static inline void mark(u16 tile)
{
    if (!dirty_flag[tile]) {
        dirty_flag[tile] = 1;
        dirty_q[dirty_n++] = tile;
    }
}

static inline bool trail_at(u16 tile, u16 p)
{
    u16 bit = tile * 64 + p;
    return trail_bits[bit >> 3] & (1 << (bit & 7));
}

static void paint(u8 step, u8 v, bool retire);

static void set_glow_palette(void)
{
    u16 cols[GLOW_SLOTS];
    u16 cur = (tick / GLOW_Q) % GLOW_SLOTS;

    for (u16 k = 0; k < GLOW_SLOTS; k++)
        cols[k] = theme->sweep ? theme->glow[(cur + GLOW_SLOTS - k) % GLOW_SLOTS] : 0;
    PAL_setColors(PAL0 * 16 + C_GLOW0, cols, GLOW_SLOTS, CPU);
}

void scope_set_theme(const struct theme *t)
{
    bool was = theme && theme->sweep;

    theme = t;
    for (u16 p = 0; p < 4; p++)
        if (p != PAL0) PAL_setColors(p * 16, t->pal[p], 16, CPU);
    /* PAL0 except the glow slots, which follow the sweep */
    PAL_setColors(PAL0 * 16, t->pal[PAL0], C_GLOW0, CPU);
    PAL_setColors(PAL0 * 16 + C_TRAIL, &t->pal[PAL0][C_TRAIL], 16 - C_TRAIL, CPU);
    set_glow_palette();
    if (was && !t->sweep)
        for (u16 a = 0; a < SCOPE_STEPS; a++) paint(a, C_BG, TRUE);   /* drop the tail */
}

void scope_init(const struct theme *t, u16 base)
{
    const u16 *map = (const u16 *)scope_map_rom;

    vram_base = base;
    memcpy(tiles, scope_tiles_rom, sizeof(tiles));
    memset(trail_bits, 0, sizeof(trail_bits));
    memset(trail_cnt, 0, sizeof(trail_cnt));
    memset(dirty_flag, 0, sizeof(dirty_flag));
    dirty_n = 0;
    tick = 0;
    theme = NULL;
    VDP_loadTileData((const u32 *)tiles, vram_base, NTILES, DMA);
    for (u16 i = 0; i < CELLS * CELLS; i++) {
        cell_tile[i] = map[i];
        VDP_setTileMapXY(BG_B, map[i] ? TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, vram_base + map[i] - 1) : 0,
                         SCOPE_X / 8 + i % CELLS, SCOPE_Y / 8 + i / CELLS);
    }
    scope_set_theme(t);
}

u16 scope_vram_end(void) { return vram_base + NTILES; }
u8 scope_step(void) { return tick & 0xFF; }

/* CPU copy in V-Blank: per-tile DMA setup (+ Z80 bus handshake) costs more
 * than writing 8 longs to the data port. */
static void flush(void)
{
    u16 n = dirty_n > FLUSH_MAX ? FLUSH_MAX : dirty_n;
    vu32 *data = (vu32 *)VDP_DATA_PORT;
    vu32 *ctrl = (vu32 *)VDP_CTRL_PORT;

    VDP_setAutoInc(2);
    while (n--) {
        u16 t = dirty_q[--dirty_n];
        const u32 *src = tiles + t * 8;
        dirty_flag[t] = 0;
        *ctrl = VDP_WRITE_VRAM_ADDR((u32)(vram_base + t) * 32);
        *data = src[0]; *data = src[1]; *data = src[2]; *data = src[3];
        *data = src[4]; *data = src[5]; *data = src[6]; *data = src[7];
    }
}

void scope_upload(void)
{
    flush();
    if (theme->sweep && tick % GLOW_Q == 0) set_glow_palette();
}

/* base[*d++] |= m, n times: move.w (d)+ / or.b m,(base,d0.w) / dbra, ~36 cycles. */
static inline const s16 *nib_or(const s16 *d, u8 *base, u16 n, u8 m)
{
    if (!n) return d;
    n--;
    __asm__ volatile (
        "1: move.w (%0)+,%%d0\n\t"
        "   or.b   %3,(0,%1,%%d0.w)\n\t"
        "   dbra   %2,1b"
        : "+a" (d), "+a" (base), "+d" (n) : "d" (m) : "d0", "cc", "memory");
    return d;
}

/* base[*d++] &= m, n times. */
static inline const s16 *nib_and(const s16 *d, u8 *base, u16 n, u8 m)
{
    if (!n) return d;
    n--;
    __asm__ volatile (
        "1: move.w (%0)+,%%d0\n\t"
        "   and.b  %3,(0,%1,%%d0.w)\n\t"
        "   dbra   %2,1b"
        : "+a" (d), "+a" (base), "+d" (n) : "d" (m) : "d0", "cc", "memory");
    return d;
}

/* Paint one sweep step. The step's data (scopegen) is: nseg, then per tile
 * segment: tile, n_even, n_odd, even byte offsets, odd byte offsets, all into
 * the RAM tile copy. Drawing ORs the glow slot into nibbles that are known to
 * be 0 (retired a full turn ago); retiring ANDs them back to 0. Tiles holding
 * trail dots take the slow path so dots survive. ~300 px per frame. */
static void paint(u8 step, u8 v, bool retire)
{
    const s16 *d = (const s16 *)sweep_px_rom + ((const u16 *)sweep_idx_rom)[step];
    u8 *base = (u8 *)tiles;
    u8 hi = v << 4;
    u16 nseg = *d++;

    while (nseg--) {
        u16 t = *d++, ne = *d++, no = *d++;
        mark(t);
        if (!trail_cnt[t]) {
            if (retire) {
                d = nib_and(d, base, ne, 0x0F);
                d = nib_and(d, base, no, 0xF0);
            } else {
                d = nib_or(d, base, ne, hi);
                d = nib_or(d, base, no, v);
            }
            continue;
        }
        for (u16 k = 0; k < ne + no; k++) {
            s16 off = *d++;
            u16 bit = off * 2 + (k >= ne);
            u8 n = v;
            if (retire) n = (trail_bits[bit >> 3] & bitmask[bit & 7]) ? C_TRAIL : C_BG;
            base[off] = (k >= ne) ? ((base[off] & 0xF0) | n) : ((base[off] & 0x0F) | (n << 4));
        }
    }
}

/* Two steps every other frame: neighbouring steps share most tiles, so the
 * upload halves; the line moves at 30 Hz, the rotation period is unchanged. */
void scope_advance(void)
{
    tick++;
    if (!(tick & 1) || !theme->sweep) return;
    for (u16 k = 0; k < 2; k++) {
        u16 t = tick - 1 + k;
        /* retire the tail end first: its slot is about to be reused */
        paint((t - TAIL) & 0xFF, C_BG, TRUE);
        paint(t & 0xFF, C_GLOW0 + (t / GLOW_Q) % GLOW_SLOTS, FALSE);
    }
}

void scope_dot(s16 px, s16 py, bool on)
{
    for (s16 dy = 0; dy < 2; dy++)
        for (s16 dx = 0; dx < 2; dx++) {
            s16 x = px + dx, y = py + dy;
            if (x < 0 || y < 0 || x >= CELLS * 8 || y >= CELLS * 8) continue;
            u16 ct = cell_tile[(y >> 3) * CELLS + (x >> 3)];
            if (!ct) continue;
            u16 t = ct - 1, p = (y & 7) * 8 + (x & 7), bit = t * 64 + p;
            u8 base = px_get(scope_tiles_rom + t * 32, p);
            if (base) continue;                     /* never draw over rings */
            u8 m = bitmask[bit & 7], had = trail_bits[bit >> 3] & m;
            if (on && !had) { trail_bits[bit >> 3] |= m; trail_cnt[t]++; }
            if (!on && had) { trail_bits[bit >> 3] &= ~m; trail_cnt[t]--; }
            px_set(t, p, on ? C_TRAIL : C_BG);
            mark(t);
        }
}

void scope_clear_dots(void)
{
    for (u16 t = 0; t < NTILES; t++)
        for (u16 b = 0; b < 8; b++) {
            u8 bits = trail_bits[t * 8 + b];
            if (!bits) continue;
            for (u16 k = 0; k < 8; k++)
                if (bits & (1 << k)) px_set(t, b * 8 + k, C_BG);
            trail_bits[t * 8 + b] = 0;
            mark(t);
        }
    memset(trail_cnt, 0, sizeof(trail_cnt));
}
