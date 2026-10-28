#include "traffic.h"
#include "feed.h"
#include "geo.h"
#include "theme.h"

#define MISS_DROP   2           /* polls absent before an aircraft is removed */

struct track tracks[TR_MAX];
volatile u8 tr_state[TR_MAX];
volatile u8 tr_brg[TR_MAX];

static struct track *find(const u8 icao[3])
{
    for (u16 i = 0; i < TR_MAX; i++) {
        struct track *t = &tracks[i];
        if (t->used && t->next.icao[0] == icao[0] && t->next.icao[1] == icao[1] &&
                t->next.icao[2] == icao[2])
            return t;
    }
    return NULL;
}

static struct track *alloc(void)
{
    for (u16 i = 0; i < TR_MAX; i++)
        if (!tracks[i].used) return &tracks[i];
    return NULL;
}

static u8 band_pal(const struct ar_rec *r)
{
    if (r->flags & AR_F_EMERG) return PAL_TEXT_EMERG;
    if ((r->flags & (AR_F_GROUND | AR_F_ALT_UNK)) || r->alt < 400) return PAL_TEXT_LOW;  /* < FL100 */
    if (r->alt < 720) return PAL_TEXT_MID;                                              /* < FL180 */
    return PAL_TEXT_HIGH;
}

static void put3(char *p, u16 v)
{
    p[0] = '0' + v / 100 % 10;
    p[1] = '0' + v / 10 % 10;
    p[2] = '0' + v % 10;
}

/* "SWA271  " / "016 174 ": callsign; alt in hundreds of ft (GND/---), gs kt. */
static void make_tag(char tag[2][TAG_W], const struct ar_rec *r)
{
    memcpy(tag[0], r->cs, TAG_W);
    memset(tag[1], ' ', TAG_W);
    if (r->flags & AR_F_GROUND) memcpy(tag[1], "GND", 3);
    else if (r->flags & AR_F_ALT_UNK) memcpy(tag[1], "---", 3);
    else put3(tag[1], (r->alt < 0 ? 0 : r->alt) >> 2);
    put3(tag[1] + 4, (r->flags & AR_F_GS_UNK) ? 0 : r->gs * 2);
    if (tag[1][4] == '0') tag[1][4] = ' ';
}

void traffic_ingest(const struct ar_hdr *h)
{
    static u8 seen[TR_MAX];

    memset(seen, 0, sizeof(seen));
    for (u16 i = 0; i < h->count; i++) {
        const struct ar_rec *r = feed_rec(h, i);
        struct track *t = find(r->icao);
        bool fresh = !t;

        if (fresh && !(t = alloc())) break;
        u8 brg = geo_angle(r->x, r->y);
        char tag[2][TAG_W];
        make_tag(tag, r);
        SYS_disableInts();
        if (fresh) {
            /* the VInt only looks at used slots, so set up everything first */
            memset(t, 0, sizeof(*t));
            t->tag_x = t->tag_y = -1;
            for (u16 k = 0; k < TRAIL_N; k++) t->dot_x[k] = -32768;   /* NO_DOT */
        }
        t->next = *r;
        memcpy(t->next_tag, tag, sizeof(tag));
        t->next_pal = band_pal(r);
        t->next_brg = brg;
        t->has_next = 1;
        t->missed = 0;
        t->drop = 0;
        t->used = 1;
        tr_brg[t - tracks] = brg;
        tr_state[t - tracks] = TS_USED | TS_NEXT;
        SYS_enableInts();
        seen[t - tracks] = 1;
    }
    for (u16 i = 0; i < TR_MAX; i++) {
        struct track *t = &tracks[i];
        if (t->used && !seen[i] && ++t->missed >= MISS_DROP) {
            SYS_disableInts();
            t->drop = 1;
            tr_state[i] |= TS_DROP;
            SYS_enableInts();
        }
    }
}

u16 traffic_count(void)
{
    u16 n = 0;
    for (u16 i = 0; i < TR_MAX; i++)
        if (tracks[i].used && !tracks[i].drop) n++;
    return n;
}
