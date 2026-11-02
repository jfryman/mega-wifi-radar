/* Spotter's logbook (lode/ui/logbook.md). The relay logs the traffic around home all day and serves today's summary
 * (/v1/log); the LOGBOOK tab shows it compactly in the panel, C opens it full size over the scope. The body arrives
 * in the frame buffer (feed_log) and is turned into panel / pop-up text straight away: nothing is kept but the text
 * the renderer already holds. */
#include "logbook.h"
#include "feed.h"
#include "fmt.h"
#include "popup.h"
#include "radar.h"
#include "settings.h"
#include "theme.h"
#include "tlist.h"

#define REFRESH_SECS    60
#define PANEL_N         (LIST_ROWS + 1)     /* the list's rows and its "more" row */

static const char *const months[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

static u16 first_row;
static bool have;           /* the panel rows hold the log */
static bool page;           /* the full page is up */
static u32 fetched_at;

/* Copy a space-padded field and trim it. */
static void field(char *out, const char *in, u16 n)
{
    memcpy(out, in, n);
    out[n] = 0;
    while (n && out[n - 1] == ' ') out[--n] = 0;
}

static void hm(char *out, u16 minute) { sprintf(out, "%02u:%02u", minute / 60, minute % 60); }

/* A record's value: fastest kt, highest FL, longest h/m. */
static void rec_value(char *out, u16 i, u16 v)
{
    if (i == 0) sprintf(out, "%uKT", v);
    else if (i == 1) sprintf(out, "FL%03u", v);
    else if (v >= 60) sprintf(out, "%uH%02u", v / 60, v % 60);
    else sprintf(out, "%uM", v);
}

static const char *rare_why(u8 f) { return (f & AR_R_MIL) ? "MIL" : (f & AR_R_TYPE) ? "TYPE" : "FLAG"; }

static void panel_rows(const struct ar_log *l)
{
    const struct ar_log_rec *rec = (const struct ar_log_rec *)(l + 1);
    const struct ar_log_rare *rare = (const struct ar_log_rare *)(rec + AR_LOG_RECS);
    static const char *const heads[AR_LOG_RECS] = { "FASTEST KT", "HIGHEST FL", "LONGEST MIN" };   /* units here */
    char buf[24], cs[9], v[8];
    u16 r = first_row;

    sprintf(buf, "%u %s %s", l->mday, months[(l->month - 1) % 12], settings.icao);
    radar_panel_pal(r++, buf, PAL2);
    sprintf(buf, "%u AC", l->aircraft);
    radar_panel(r++, buf);
    sprintf(buf, "%u TYPES", l->types);
    radar_panel(r++, buf);
    if (l->since) { hm(v, l->since); sprintf(buf, "SINCE %s", v); }
    else if (l->peak_n) sprintf(buf, "PEAK %02uH %u", l->peak_hour, l->peak_n);
    else buf[0] = 0;
    radar_panel_pal(r++, buf, PAL1);
    for (u16 i = 0; i < AR_LOG_RECS; i++) {
        radar_panel_pal(r++, heads[i], PAL1);
        field(cs, rec[i].cs, 7);
        if (cs[0]) sprintf(buf, "%-7s %3u", cs, rec[i].value > 999 ? 999 : rec[i].value);   /* as the list's rows */
        else strcpy(buf, "--");
        radar_panel(r++, buf);
    }
    sprintf(buf, "RARE %u", l->nrare);
    radar_panel_pal(r++, l->nrare ? buf : "NO RARES", PAL1);
    for (u16 i = 0; i < 4; i++) {
        if (i < l->nrare) {
            char ty[5];
            field(cs, rare[i].cs, 7);
            field(ty, rare[i].type, 4);
            sprintf(buf, "%-7s%4s", cs, ty);
            radar_panel_pal(r++, buf, (rare[i].rflags & AR_R_MIL) ? PAL2 : PAL0);
        } else {
            radar_panel(r++, "");
        }
    }
    radar_panel_pal(r++, "C: FULL LOG", PAL1);
    while (r < first_row + PANEL_N) radar_panel(r++, "");
}

static void page_rows(const struct ar_log *l)
{
    const struct ar_log_rec *rec = (const struct ar_log_rec *)(l + 1);
    const struct ar_log_rare *rare = (const struct ar_log_rare *)(rec + AR_LOG_RECS);
    static const char *const names[AR_LOG_RECS] = { "FASTEST", "HIGHEST", "LONGEST" };
    char buf[POPUP_COLS + 1], cs[9], ty[5], v[12], t[8];

    for (u16 r = 0; r < PANEL_ROWS; r++) radar_popup_row(r, "", PAL0);
    sprintf(buf, " TODAY AT %-8s %2u %s", settings.icao, l->mday, months[(l->month - 1) % 12]);
    radar_popup_row(1, buf, PAL2);
    radar_popup_row(2, " --------------------------", PAL1);
    sprintf(buf, " AIRCRAFT    %u, %u TYPES", l->aircraft, l->types);
    radar_popup_row(3, buf, PAL0);
    if (l->peak_n) { sprintf(buf, " BUSIEST     %02u:00, %u NEW", l->peak_hour, l->peak_n); radar_popup_row(4, buf, PAL0); }
    if (l->since) { hm(t, l->since); sprintf(buf, " LOGGED FROM %s", t); }
    else strcpy(buf, " LOGGED FROM MIDNIGHT");
    radar_popup_row(5, buf, PAL1);
    radar_popup_row(7, " RECORDS", PAL2);
    for (u16 i = 0; i < AR_LOG_RECS; i++) {
        field(cs, rec[i].cs, 8);
        field(ty, rec[i].type, 4);
        rec_value(v, i, rec[i].value);
        if (cs[0]) sprintf(buf, " %-8s%-8s%-5s%s", names[i], cs, ty, v);
        else sprintf(buf, " %-8s--", names[i]);
        radar_popup_row(8 + i, buf, PAL0);
    }
    radar_popup_row(12, l->nrare ? " RARE TODAY" : " NO RARE AIRCRAFT YET", PAL2);
    for (u16 i = 0; i < l->nrare && i < AR_LOG_RARE; i++) {
        field(cs, rare[i].cs, 8);
        field(ty, rare[i].type, 4);
        hm(t, rare[i].minute);
        sprintf(buf, " %s  %-8s %-4s  %s", t, cs, ty, rare_why(rare[i].rflags));
        radar_popup_row(13 + i, buf, (rare[i].rflags & AR_R_MIL) ? PAL2 : PAL0);
    }
    radar_popup_row(22, " WITHIN 25 NM, KEPT BY", PAL1);
    radar_popup_row(23, " THE RELAY ALL DAY", PAL1);
    radar_popup_row(25, "                 C/B CLOSE", PAL1);
}

static void loading(const char *why)
{
    radar_panel_pal(first_row, why, PAL1);
    for (u16 r = 1; r < PANEL_N; r++) radar_panel(first_row + r, "");
}

void logbook_panel(u16 row)
{
    if (row != first_row) have = FALSE;
    first_row = row;
    if (!have) loading("LOADING...");
}

void logbook_forget(void) { have = FALSE; }

void logbook_fetch(void)
{
    const struct ar_log *l;

    fetched_at = vtimer;
    if (feed_log(&l) != FEED_OK) {
        if (!have && settings.sort == SORT_LOGBOOK) loading("NO LOG");
        return;
    }
    if (settings.sort == SORT_LOGBOOK) {
        panel_rows(l);
        have = TRUE;
    }
    if (page) page_rows(l);
}

void logbook_poll(void)
{
    if ((settings.sort == SORT_LOGBOOK || page) && (!have || vtimer - fetched_at >= REFRESH_SECS * 60UL))
        logbook_fetch();
}

void logbook_page(bool open)
{
    if (open == page) return;
    page = open;
    if (open) {
        popup_close();                          /* they share the window */
        for (u16 r = 0; r < PANEL_ROWS; r++) radar_popup_row(r, "", PAL0);
        radar_popup_row(1, " LOADING THE LOGBOOK...", PAL1);
        radar_popup(TRUE);
        logbook_fetch();
    } else {
        radar_popup(FALSE);
    }
}

bool logbook_page_open(void) { return page; }
