/* Master SH-2: the 32X boot splash (src/splash.h; lode/ui/boot-splash.md for the Genesis one it retells). A dawn
 * airfield in 256 colours: a 787 at the gate taxis to runway 9, holds, and departs as boot progresses, with the
 * same ATC calls; the slave sets the phase (sh/splash32.c). */
#include "render.h"
#include "draw.h"
#include "mars.h"
#include "shared.h"
#include "splash.h"
#include "settings.h"
#include "airports.h"

#define C_SKY       1       /* 1..80: sky, top to the horizon */
#define N_SKY       80
#define C_GROUND    90      /* 90..105: ground, horizon to the bottom */
#define C_CITY      110
#define C_CITY_LIT  111
#define C_RUNWAY    112
#define C_STRIPE    113
#define C_EDGE      114     /* 114..117: runway lights, twinkling */
#define C_BODY      120
#define C_BELLY     121
#define C_WING      122
#define C_TAIL      123
#define C_WIN       124
#define C_RADIO     126
#define C_RADIO_EDGE 127
#define C_LOGO      130
#define C_LOGO2     131
#define C_BADGE     132
#define C_SUN       133     /* 133..140: the sun's glow */
#define HORIZON     126
#define RUNWAY_Y    164
#define HOLD_X      108

enum { ROLE_PILOT, ROLE_ATC, ROLE_RED };

static s32 plane_x, plane_y;        /* 16.16 px: centre of the fuselage */
static s32 speed;                   /* 16.16 px per frame */
static s16 pitch;                   /* 1/256 turn, nose up */
static u32 last, fin_at;
static u8 fin_stage;                /* 0 roll, 1 climb, 2 contact, 3 fade, 4 done */
static char city[16];

static const s16 sin_q14[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811,
    14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340,
    16364, 16379, 16384,
};

static void palette(u16 fade)       /* fade 0..16: 16 = full brightness */
{
    static const u16 txt[4] = { RGB(30, 30, 30), RGB(10, 28, 31), RGB(10, 28, 31), RGB(31, 10, 8) };
    u16 black = 0;
    #define P(i, c) pal_set(i, rgb_mix(black, (c), fade, 16))
    for (u16 i = 0; i < N_SKY; i++) {           /* night blue to a dawn glow at the horizon */
        u16 c = i < 40 ? rgb_mix(RGB(1, 2, 8), RGB(9, 6, 16), i, 40) : rgb_mix(RGB(9, 6, 16), RGB(30, 16, 8), i - 40, 40);
        P(C_SKY + i, c);
    }
    for (u16 i = 0; i < 16; i++) P(C_GROUND + i, rgb_mix(RGB(5, 5, 7), RGB(1, 2, 2), i, 15));
    P(C_CITY, RGB(3, 3, 7));
    P(C_CITY_LIT, RGB(30, 24, 10));
    P(C_RUNWAY, RGB(6, 6, 7));
    P(C_STRIPE, RGB(24, 24, 22));
    P(C_BODY, RGB(29, 29, 30));
    P(C_BELLY, RGB(16, 17, 20));
    P(C_WING, RGB(12, 13, 16));
    P(C_TAIL, RGB(4, 8, 22));
    P(C_WIN, RGB(6, 10, 18));
    P(C_RADIO, RGB(1, 2, 5));
    P(C_RADIO_EDGE, RGB(6, 10, 22));
    P(C_LOGO, RGB(31, 31, 31));
    P(C_LOGO2, RGB(14, 22, 31));
    P(C_BADGE, RGB(28, 3, 3));
    for (u16 i = 0; i < 8; i++) P(C_SUN + i, rgb_mix(RGB(31, 20, 8), RGB(30, 16, 8), i, 7));
    for (u16 i = 0; i < 4; i++) P(C_TXT(i), txt[i]);
    P(C_SHADOW, 0);
    P(C_DIM, RGB(14, 14, 16));
    #undef P
}

static void lights(u16 t)
{
    for (u16 i = 0; i < 4; i++) {
        u16 k = (t / 6 + i * 3) & 15;
        pal_set(C_EDGE + i, k < 2 ? RGB(31, 31, 20) : RGB(24, 14, 4));
    }
}

void rsplash_enter(void)
{
    s16 a = airport_find(settings.icao);
    const char *name = a >= 0 ? airports[a].name : settings.icao;
    u16 i = 0;
    while (i < sizeof(city) - 1 && name[i] && name[i] != ' ') { city[i] = name[i]; i++; }
    city[i] = 0;
    plane_x = (sh.sp.phase >= SP_HOLD ? HOLD_X : 46) << 16;
    plane_y = (RUNWAY_Y - 6) << 16;
    speed = 0;
    pitch = 0;
    fin_stage = 0;
    last = mars_vtimer;
    palette(16);
}

/* ------------------------------------------------------------ scene */

static void scenery(u16 t)
{
    for (u16 i = 0; i < N_SKY / 2; i++) fb_fill_rows(i * HORIZON / (N_SKY / 2), HORIZON / (N_SKY / 2) + 1, C_SKY + i * 2);
    for (u16 r = 0; r < 4; r++)                         /* the sun just under the horizon */
        for (s16 y = 0; y < 12 - r * 3; y++) {
            s16 w = (60 - r * 14) - y * 3;
            if (w > 0) fb_hline(250 - w, 250 + w, HORIZON - 1 - y, C_SUN + r * 2);
        }
    static const u8 city_h[20] = { 9, 14, 7, 18, 11, 22, 8, 13, 26, 10, 16, 6, 19, 12, 24, 9, 15, 7, 11, 5 };
    for (u16 i = 0; i < 20; i++) {                      /* skyline, with a few lit windows */
        s16 x = 4 + i * 11, h = city_h[i];
        fb_rect(x, HORIZON - h, 10, h, C_CITY);
        if ((i * 7 + t / 90) % 3 == 0) fb_px(x + 3, HORIZON - h + 3, C_CITY_LIT);
        if ((i * 5) % 4 == 1) fb_px(x + 6, HORIZON - h + 6, C_CITY_LIT);
    }
    for (u16 i = 0; i < 16; i++) fb_fill_rows(HORIZON + i * 6, 6, C_GROUND + i);
    fb_rect(0, RUNWAY_Y + 2, FB_W, 12, C_RUNWAY);       /* runway 9, from the left edge to the right */
    for (s16 x = (s16)(-(t * 0) % 24); x < FB_W; x += 24) fb_rect(x + 4, RUNWAY_Y + 7, 12, 2, C_STRIPE);
    fb_text(4, RUNWAY_Y + 5, "9", C_TXT(0), 0);
    for (s16 x = 6; x < FB_W; x += 16) {
        fb_px(x, RUNWAY_Y + 1, C_EDGE + (x / 16) % 4);
        fb_px(x, RUNWAY_Y + 14, C_EDGE + (x / 16 + 2) % 4);
    }
    fb_rect(HOLD_X + 30, RUNWAY_Y - 6, 2, 8, C_STRIPE); /* hold-short bar */
}

/* rotate (dx, dy) by pitch (nose up = counter-clockwise on screen) */
static void rot(s16 dx, s16 dy, s16 *ox, s16 *oy)
{
    s16 s = sin_q14[pitch < 0 ? 0 : pitch > 64 ? 64 : pitch], c = sin_q14[64 - (pitch < 0 ? 0 : pitch > 64 ? 64 : pitch)];
    *ox = (s16)(((s32)dx * c + (s32)dy * s) >> 14);
    *oy = (s16)(((s32)dy * c - (s32)dx * s) >> 14);
}

static void tri(s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2, u8 col)
{
    /* scanline fill, edges sorted by y */
    #define SW(a, b) do { s16 t_ = a; a = b; b = t_; } while (0)
    if (y1 < y0) { SW(x0, x1); SW(y0, y1); }
    if (y2 < y0) { SW(x0, x2); SW(y0, y2); }
    if (y2 < y1) { SW(x1, x2); SW(y1, y2); }
    #undef SW
    for (s16 y = y0; y <= y2; y++) {
        s16 xa = y2 != y0 ? x0 + (s32)(x2 - x0) * (y - y0) / (y2 - y0) : x0, xb;
        if (y < y1) xb = y1 != y0 ? x0 + (s32)(x1 - x0) * (y - y0) / (y1 - y0) : x1;
        else xb = y2 != y1 ? x1 + (s32)(x2 - x1) * (y - y1) / (y2 - y1) : x1;
        fb_hline(xa, xb, y, col);
    }
}

/* A 787 side view, nose to the right, about scale/16 of 96 px long. */
static void plane(s16 cx, s16 cy, u16 scale)
{
    #define PT(dx, dy, X, Y) do { s16 a_, b_; rot((dx) * (s16)scale / 16, (dy) * (s16)scale / 16, &a_, &b_); X = cx + a_; Y = cy + b_; } while (0)
    s16 ax, ay, bx, by, qx, qy;
    PT(-40, -2, ax, ay); PT(-30, -20, bx, by); PT(-22, -2, qx, qy);
    tri(ax, ay, bx, by, qx, qy, C_TAIL);                /* fin */
    PT(-6, 2, ax, ay); PT(10, 2, bx, by); PT(-14, 12, qx, qy);
    tri(ax, ay, bx, by, qx, qy, C_WING);                /* wing, swept back */
    for (s16 k = -4; k <= 4; k++) {                     /* fuselage: tapered tail to rounded nose */
        s16 tl = k < -2 || k > 2 ? -34 : -44, nose = 46 - k * k / 2;
        PT(tl, k, ax, ay); PT(nose, k, bx, by);
        fb_line(ax, ay, bx, by, k > 2 ? C_BELLY : C_BODY);
        PT(tl, k, ax, ay); PT(nose, k, bx, by);
        fb_line(ax, ay + 1, bx, by + 1, k > 2 ? C_BELLY : C_BODY);
    }
    for (s16 w = -28; w < 34; w += 5) { PT(w, -1, ax, ay); fb_px(ax, ay, C_WIN); }
    PT(-2, 6, ax, ay);                                  /* engine */
    fb_rect(ax - 5 * (s16)scale / 16, ay, 11 * (s16)scale / 16 + 1, 4 * (s16)scale / 16 + 1, C_BELLY);
    #undef PT
}

/* ------------------------------------------------------------ narration */

static void say(u16 line, u16 role, const char *s)
{
    fb_text(16, 188 + line * 10, s, C_TXT(role == ROLE_PILOT ? 0 : role == ROLE_ATC ? 1 : 3), 0);
}

static void script(u16 p)
{
    char buf[64];
    switch (p) {
    case SP_GATE:
    case SP_NO_MODULE:
        sprintf(buf, "%s GROUND, MEGA 787, AT GATE,", city);
        say(0, ROLE_PILOT, buf);
        if (p == SP_GATE) say(1, ROLE_PILOT, "RADIO CHECK.");
        else { sprintf(buf, "NO REPLY: %s", sh.sp.detail); say(1, ROLE_RED, buf); }
        break;
    case SP_TAXI:
        sprintf(buf, "MEGA 787, %s GROUND, FIVE BY", city);
        say(0, ROLE_ATC, buf);
        say(1, ROLE_ATC, "FIVE. TAXI RUNWAY 9 VIA ALPHA.");
        break;
    case SP_HOLD:
        say(0, ROLE_ATC, "MEGA 787, HOLD SHORT RUNWAY 9,");
        say(1, ROLE_ATC, "CONTACT TOWER 118.7.");
        break;
    case SP_RELAY:
        say(0, ROLE_PILOT, "TOWER, MEGA 787, HOLDING SHORT,");
        say(1, ROLE_PILOT, "REQUEST RADAR SERVICE.");
        break;
    case SP_HOLD_POS:
        say(0, ROLE_ATC, "MEGA 787, HOLD POSITION.");
        say(1, ROLE_RED, sh.sp.detail);
        break;
    case SP_TAKEOFF:
        sprintf(buf, "MEGA 787, %s TOWER, RUNWAY 9,", city);
        say(0, ROLE_ATC, buf);
        say(1, ROLE_ATC, "WIND 090 AT 8, CLEARED FOR TAKEOFF.");
        break;
    }
}

/* ------------------------------------------------------------ frame */

static void move(u16 frames)
{
    u8 p = sh.sp.phase;
    while (frames--) {
        if (sh.sp.finishing) {
            if (fin_stage == 0) {                       /* the roll */
                speed += 0x0800;
                if (plane_x > (190 << 16)) { fin_stage = 1; }
            } else if (fin_stage == 1) {                /* rotate and climb */
                speed += 0x0400;
                if (pitch < 18) pitch++;
                plane_y -= speed * pitch / 48;
                if (plane_y < -(40 << 16) || plane_x > (360 << 16)) { fin_stage = 2; fin_at = mars_vtimer; }
            } else if (fin_stage == 2 && mars_vtimer - fin_at > 100) {
                fin_stage = 3; fin_at = mars_vtimer;
            } else if (fin_stage == 3) {
                u16 k = (mars_vtimer - fin_at) / 2;
                palette(k >= 16 ? 0 : 16 - k);
                if (k >= 16) { fin_stage = 4; sh.sp.done = 1; }
            }
        } else if (p == SP_TAXI && plane_x < (HOLD_X << 16)) {
            if (speed < 0x6000) speed += 0x0400;
        } else if (p == SP_TAKEOFF) {
            speed = speed < 0x2000 ? speed + 0x0200 : speed;  /* lining up */
        } else {
            speed = speed > 0x0800 ? speed - 0x0800 : 0;
        }
        if (!sh.sp.finishing && p != SP_TAKEOFF && plane_x > (HOLD_X << 16)) { plane_x = HOLD_X << 16; speed = 0; }
        plane_x += speed;
    }
}

void rsplash_frame(void)
{
    u32 now = mars_vtimer;
    move(now - last);
    last = now;
    lights(now);
    scenery(now);

    fb_text_big(16, 14, "MEGA WIFI RADAR", 2, C_LOGO, C_SHADOW);
    fb_rect(260, 12, 48, 20, C_BADGE);                  /* the 32X badge */
    fb_text_big(262, 14, "32X", 2, C_LOGO, 0);
    fb_hline(16, 304, 34, C_LOGO2);
    fb_text(16, 38, "LIVE ATC RADAR OVER MEGAWIFI", C_LOGO2, 0);

    if (fin_stage < 2) plane(plane_x >> 16, plane_y >> 16, fin_stage == 1 ? 16 - pitch / 3 : 16);

    fb_rect(8, 182, 304, 36, C_RADIO);                  /* radio box */
    fb_frame(8, 182, 304, 36, C_RADIO_EDGE);
    if (sh.sp.finishing && fin_stage >= 2) {
        say(0, ROLE_ATC, "MEGA 787, RADAR CONTACT.");
        say(1, ROLE_ATC, "GOOD DAY.");
    } else if (sh.sp.finishing) {
        say(0, ROLE_PILOT, "CLEARED FOR TAKEOFF RUNWAY 9,");
        say(1, ROLE_PILOT, "MEGA 787.");
    } else {
        script(sh.sp.phase);
    }
    fb_text(16, 210, sh.sp.status, C_DIM, 0);
}
