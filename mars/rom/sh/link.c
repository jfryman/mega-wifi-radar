/* COMM link, slave SH-2 side (lode/mars/link.md): blocking RPCs to the 68000's I/O server. */
#include "link_sh.h"
#include "../link.h"
#include "mars.h"
#include "shared.h"

static u8 mine;                         /* our COMM0 low byte */

static u8 md_ctrl(void) { return MARS_COMM(LK_MD_CTRL) & 0xFF; }
static void set_ctrl(u8 v) { mine = v; MARS_COMM(LK_SH_CTRL) = v; }

void lk_wait_ready(void)
{
    while ((MARS_COMM(LK_MD_CTRL) >> 8) != LK_ST_READY) {}
    set_ctrl(md_ctrl());
}

static void tx_block(u16 w0, u16 w1, u16 w2)
{
    MARS_COMM(LK_SH_DATA) = w0;
    MARS_COMM(LK_SH_DATA + 2) = w1;
    MARS_COMM(LK_SH_DATA + 4) = w2;
    u8 was = md_ctrl();
    set_ctrl(was + 1);
    while (md_ctrl() == was) {}         /* acked once it moves off `was` (md/link.c) */
}

static void rx_block(u16 w[2])
{
    while (md_ctrl() == mine) {}
    w[0] = MARS_COMM(LK_MD_DATA);
    w[1] = MARS_COMM(LK_MD_DATA + 2);
    set_ctrl(md_ctrl());
}

u8 lk_call(u8 cmd, const void *args, u16 arglen, void *reply, u32 max, u16 *got)
{
    const u8 *a = args;
    u8 *r = reply;
    u16 w[2], len;

    sh.dbg[0] = cmd;
    sh.dbg[1] = 1;
    tx_block(cmd << 8, arglen, 0);
    for (u16 i = 0; i < arglen; i += 6) {
        u8 b[6];
        for (u16 k = 0; k < 6; k++) b[k] = i + k < arglen ? a[i + k] : 0;
        tx_block(b[0] << 8 | b[1], b[2] << 8 | b[3], b[4] << 8 | b[5]);
    }
    sh.dbg[1] = 2;
    rx_block(w);
    sh.dbg[1] = 3;
    u8 status = w[0] >> 8;
    len = w[1];
    for (u32 n = 0; n < len; n += 4) {
        rx_block(w);
        u8 b[4] = { w[0] >> 8, w[0], w[1] >> 8, w[1] };
        for (u16 k = 0; k < 4; k++)
            if (n + k < len && n + k < max) r[n + k] = b[k];
    }
    sh.dbg[1] = 4;
    sh.dbg[2] = len;
    if (got) *got = len < max ? len : max;
    return status;
}
