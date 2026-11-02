#include "alert.h"
#include "chime.h"
#include "fmt.h"
#include "geo.h"
#include "notice.h"
#include "traffic.h"

#define GAP_SECS    60      /* between alerts; a second emergency waits its turn */
#define SHOW_SECS   20

static u32 last_at;
static bool any;

/* Copy s into a line, space padded; returns the column after it. */
static u16 put(char *line, u16 col, const char *s)
{
    while (*s && col < AR_NOTICE_COLS) line[col++] = *s++;
    return col;
}

static void compose(struct ar_notice *n, const struct ar_rec *r)
{
    static const char *const pt[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    char buf[16], *l1 = n->text, *l2 = l1 + AR_NOTICE_COLS, *l3 = l2 + AR_NOTICE_COLS;
    u16 c;

    memset(n, 0, sizeof(*n));
    memcpy(n->magic, "AN", 2);
    n->ver = AR_VER;
    n->nflags = AR_N_ICAO | AR_N_EMERG;
    n->id = 1;
    memcpy(n->icao, r->icao, 3);
    n->secs = SHOW_SECS;
    memset(n->text, ' ', sizeof(n->text));

    put(l1, 0, r->squawk == 0x7700 ? "SQUAWK 7700 EMERGENCY" : r->squawk == 0x7600 ? "SQUAWK 7600 RADIO FAIL" :
               r->squawk == 0x7500 ? "SQUAWK 7500 HIJACK" : "EMERGENCY");

    memcpy(l2, r->cs, 8);                   /* "AAL1140   12 NM NE" */
    u16 d10 = ((u32)isqrt32((u32)((s32)r->x * r->x) + (u32)((s32)r->y * r->y)) * 10) >> 5;
    if (d10 < 100) sprintf(buf, "%u.%u NM ", d10 / 10, d10 % 10);
    else sprintf(buf, "%u NM ", d10 / 10);
    c = put(l2, 10, buf);
    put(l2, c, pt[((geo_angle(r->x, r->y) + 16) >> 5) & 7]);

    if (r->flags & AR_F_GROUND) c = put(l3, 0, "ON GROUND");   /* "8,500FT 250KT", as the flyover notice */
    else if (r->flags & AR_F_ALT_UNK) c = put(l3, 0, "ALT ?");
    else {
        fmt_thousands(buf, (s32)r->alt * 25);
        c = put(l3, put(l3, 0, buf), "FT");
    }
    if (!(r->flags & AR_F_GS_UNK)) {
        sprintf(buf, " %uKT", r->gs * 2);
        put(l3, c, buf);
    }
}

void alert_check(void)
{
    if (any && vtimer - last_at < GAP_SECS * 60UL) return;
    for (u16 i = 0; i < TR_MAX; i++) {
        struct track *t = &tracks[i];
        if (!t->used || t->drop || t->emerg != 2) continue;
        struct ar_notice n;
        compose(&n, &t->next);
        t->emerg = 3;                       /* until the flag clears (traffic_ingest) */
        notice_local(&n);
        chime_alert();
        last_at = vtimer;
        any = TRUE;
        return;
    }
}
