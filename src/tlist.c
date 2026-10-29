#include "tlist.h"
#include "fmt.h"
#include "radar.h"
#include "settings.h"
#include "theme.h"
#include "traffic.h"

static const char *const sort_names[SORT_MODES] = {
    "- NEAREST -", "--- A-Z ---", "--- Z-A ---", "- LOW ALT -", "- HIGH ALT",
};

static u8 sort_mode = SORT_NEAREST;     /* RAM only: every boot starts at NEAREST */
static u8 order[TR_MAX];                /* track slots in list order */
static u16 dist[TR_MAX];                /* per slot: distance from home, 1/32 nm */
static u16 count, top, first_row;
static bool drawn;

/* Altitude sort key: ground lowest; unknown altitude sorts after every known one in both orders. */
#define ALT_GROUND  (-32767)
#define ALT_UNKNOWN 32767
static s16 alt_key(const struct ar_rec *r)
{
    if (r->flags & AR_F_GROUND) return ALT_GROUND;
    if (r->flags & AR_F_ALT_UNK) return ALT_UNKNOWN;
    return r->alt;
}

static bool listed(u16 i) { return tracks[i].used && !tracks[i].drop; }

/* TRUE if slot a goes before slot b. Emergencies first in every order; ties keep slot order. */
static bool before(u8 a, u8 b)
{
    const struct ar_rec *ra = &tracks[a].next, *rb = &tracks[b].next;
    bool ea = ra->flags & AR_F_EMERG, eb = rb->flags & AR_F_EMERG;
    s16 c = 0;

    if (ea != eb) return ea;
    switch (sort_mode) {
    case SORT_NEAREST: c = dist[a] < dist[b] ? -1 : dist[a] > dist[b]; break;
    case SORT_AZ: c = memcmp(ra->cs, rb->cs, sizeof(ra->cs)); break;
    case SORT_ZA: c = memcmp(rb->cs, ra->cs, sizeof(ra->cs)); break;
    case SORT_ALT_LOW:
    case SORT_ALT_HIGH: {
        s16 ka = alt_key(ra), kb = alt_key(rb);
        if ((ka == ALT_UNKNOWN) != (kb == ALT_UNKNOWN)) return kb == ALT_UNKNOWN;
        c = ka < kb ? -1 : ka > kb;
        if (sort_mode == SORT_ALT_HIGH) c = -c;
        break;
    }
    }
    return c < 0 || (c == 0 && a < b);
}

static s16 index_of(s16 slot)
{
    for (u16 k = 0; k < count; k++)
        if (order[k] == slot) return k;
    return -1;
}

/* Keep the selection on screen. */
static void scroll_to(s16 k)
{
    if (k < 0) k = 0;
    if (k < top) top = k;
    if (k >= top + LIST_ROWS) top = k - LIST_ROWS + 1;
    if (top + LIST_ROWS > count) top = count > LIST_ROWS ? count - LIST_ROWS : 0;
}

/* "UAL2414 0.5" / "SWA4720 19": the callsign is cut to 7 so a gap always precedes the distance. */
static void entry(char *buf, u8 slot)
{
    u16 d10 = ((u32)dist[slot] * 10) >> 5;

    memcpy(buf, tracks[slot].next.cs, 7);
    buf[7] = ' ';
    if (d10 < 100) sprintf(buf + 8, "%u.%u", d10 / 10, d10 % 10);
    else sprintf(buf + 8, "%3u", d10 / 10 > 999 ? 999 : d10 / 10);
}

void tlist_draw(u16 row)
{
    s16 sel = radar_selected();
    char buf[16];

    first_row = row;
    drawn = TRUE;
    radar_panel(row, sort_names[sort_mode]);
    for (u16 r = 0; r < LIST_ROWS; r++) {
        u16 k = top + r;
        if (k >= count) {
            radar_panel(row + 1 + r, "");
            continue;
        }
        u8 slot = order[k];
        u16 pal = (tracks[slot].next.flags & AR_F_EMERG) ? PAL_TEXT_EMERG : slot == sel ? PAL2 : PAL0;
        entry(buf, slot);
        radar_panel_pal(row + 1 + r, buf, pal);
    }
    u16 below = count > top + LIST_ROWS ? count - top - LIST_ROWS : 0;
    char *p = buf;
    *p = 0;
    if (top) p += sprintf(p, "^%u ", top);
    if (below) sprintf(p, "v%u MORE", below);
    radar_panel(row + 1 + LIST_ROWS, buf);
}

void tlist_rebuild(void)
{
    count = 0;
    for (u16 i = 0; i < TR_MAX; i++) {
        if (!listed(i)) continue;
        s32 x = tracks[i].next.x, y = tracks[i].next.y;
        dist[i] = isqrt32((u32)(x * x) + (u32)(y * y));
        /* insertion sort: at most 96 entries, once per sweep, in the main loop */
        u16 k = count++;
        while (k && before(i, order[k - 1])) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = i;
    }
    s16 sel = radar_selected();
    if (sel >= 0 && !listed(sel)) radar_select(sel = -1);
    scroll_to(index_of(sel));
    if (drawn) tlist_draw(first_row);
}

void tlist_move(s16 d)
{
    if (!count) return;
    s16 k = index_of(radar_selected());
    k = k < 0 ? 0 : k + d;
    if (k < 0) k = 0;
    if (k >= (s16)count) k = count - 1;
    radar_select(order[k]);
    if (settings.tag_mode == TAGS_SELECTED) radar_retag();
    scroll_to(k);
    tlist_draw(first_row);
}

void tlist_sort_next(void)
{
    sort_mode = (sort_mode + 1) % SORT_MODES;
    tlist_rebuild();
}
