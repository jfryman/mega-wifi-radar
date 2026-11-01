/* COMM link, 68000 side (lode/mars/link.md): the server end of the RPC pipe to the slave SH-2. Every wait
 * polls md_tick() (pad, time) and pumps the MegaWiFi driver. */
#include "genesis.h"
#include "../link.h"
#include "link_md.h"

#define COMM(n)     (*(volatile u16 *)(0xA15120 + (n)))

static u8 mine;
extern u16 md_stage;                         /* our COMM8 low byte: sequence when sending, ack when receiving */

static void set_ctrl(u8 v)
{
    mine = v;
    COMM(LK_MD_CTRL) = (LK_ST_READY << 8) | v;
}

static u8 sh_ctrl(void) { return COMM(LK_SH_CTRL) & 0xFF; }

void lk_init(void) { set_ctrl(0); }

bool lk_request_waiting(void) { return sh_ctrl() != mine; }

/* One block from the slave: 3 words. */
static void rx_block(u16 w[3])
{
    md_stage = 2;
    while (sh_ctrl() == mine) { md_tick(); md_pump(); }
    md_stage = 3;
    w[0] = COMM(LK_SH_DATA);
    w[1] = COMM(LK_SH_DATA + 2);
    w[2] = COMM(LK_SH_DATA + 4);
    set_ctrl(sh_ctrl());
}

static void tx_block(u16 w0, u16 w1)
{
    COMM(LK_MD_DATA) = w0;
    COMM(LK_MD_DATA + 2) = w1;
    u8 was = sh_ctrl(), seq = was + 1;
    set_ctrl(seq);
    md_stage = 5;
    /* acked once it moves off `was`: it may already be past seq (the slave acks the last block of a reply and
     * starts its next request at once), so waiting for == seq could miss it for good */
    while (sh_ctrl() == was) { md_tick(); md_pump(); }
    md_stage = 6;
}

/* A request: header block (u8 cmd, u8 0, u16 len, 2 pad), then len bytes in 6-byte blocks. */
u16 lk_recv(u8 *cmd, u8 *buf, u16 max)
{
    u16 w[3], len, n = 0;

    rx_block(w);
    *cmd = w[0] >> 8;
    len = w[1];
    while (n < len) {
        rx_block(w);
        for (u16 k = 0; k < 3; k++) {
            if (n < len && n < max) buf[n] = w[k] >> 8;
            n++;
            if (n < len && n < max) buf[n] = w[k];
            n++;
        }
    }
    return len < max ? len : max;
}

/* A reply in pieces: lk_reply_begin(status, total), then lk_reply_data() until total bytes, then lk_reply_end(). */
static u8 stage[4];
static u16 staged;

void lk_reply_begin(u8 status, u16 total)
{
    tx_block(status << 8, total);
    staged = 0;
}

void lk_reply_data(const void *data, u16 len)
{
    const u8 *p = data;
    while (len--) {
        stage[staged++] = *p++;
        if (staged == 4) {
            tx_block(stage[0] << 8 | stage[1], stage[2] << 8 | stage[3]);
            staged = 0;
        }
    }
}

void lk_reply_end(void)
{
    if (!staged) return;
    while (staged < 4) stage[staged++] = 0;
    tx_block(stage[0] << 8 | stage[1], stage[2] << 8 | stage[3]);
    staged = 0;
}

void lk_reply(u8 status, const void *data, u16 len)
{
    lk_reply_begin(status, len);
    lk_reply_data(data, len);
    lk_reply_end();
}
