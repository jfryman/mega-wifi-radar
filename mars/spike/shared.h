/* 32X spike: the COMM registers shared by the 68000 and the SH-2s.
 * Byte offsets from COMM0 ($A15120 on the 68000, $20004020 on the SH-2s). Every register has
 * exactly one writer. COMM0..COMM7 carry the boot handshake ("M_OK" / "S_OK"); both are zero before
 * the slave runs, so COMM2 / COMM6 are free afterwards. */
#ifndef SPIKE_SHARED_H
#define SPIKE_SHARED_H

#define COMM_SLAVE_PAUSE	2	/* 68k -> slave SH-2: 1 = stop reading ROM */
#define COMM_SLAVE_PAUSED	6	/* slave SH-2 -> 68k: 1 = paused, off the cart bus */
#define COMM_MASTER_FRAMES	8	/* master SH-2 -> 68k: frame buffer flips */
#define COMM_SLAVE_PASSES	10	/* slave SH-2 -> 68k: ROM-read passes */
#define COMM_NET_STATE		12	/* 68k -> master SH-2: enum net_state */
#define COMM_NET_BYTES		14	/* 68k -> master SH-2: relay response size */

enum net_state { NET_IDLE = 0, NET_PENDING, NET_OK, NET_FAILED, NET_SKIPPED };

#endif
