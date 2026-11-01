#ifndef LINK_MD_H
#define LINK_MD_H
#include "genesis.h"
void lk_init(void);
bool lk_request_waiting(void);
u16 lk_recv(u8 *cmd, u8 *buf, u16 max);
void lk_reply_begin(u8 status, u16 total);
void lk_reply_data(const void *data, u16 len);
void lk_reply_end(void);
void lk_reply(u8 status, const void *data, u16 len);
#endif
