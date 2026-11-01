#ifndef LINK_SH_H
#define LINK_SH_H
#include "genesis.h"
void lk_wait_ready(void);
/* One RPC (link.h): returns the reply's status byte; up to max data bytes into reply, *got = their count. */
u8 lk_call(u8 cmd, const void *args, u16 arglen, void *reply, u32 max, u16 *got);
#endif
