/* SGDK's task API as the MegaWiFi driver uses it, without tasks (md/task.c): TSK_superPend() polls
 * mw_process() and the pad until TSK_superPost() is called from a driver callback, or the time is up. */
#ifndef MARS_TASK_H
#define MARS_TASK_H
#include "genesis.h"
#define TSK_PEND_FOREVER    -1
bool TSK_superPend(s16 wait);   /* TRUE on timeout */
void TSK_superPost(bool force_ctx_sw);
void TSK_userSet(void (*task)(void));
#endif
