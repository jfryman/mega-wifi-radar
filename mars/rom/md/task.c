/* SGDK's supervisor pend/post without tasks (compat/task.h). The Genesis runs mw_process() in a user task
 * whenever the main code waits; here the wait itself pumps it. md_mw_ready is clear while mw_init() runs: the
 * driver's state isn't valid yet (src/net.c's comment on user_tsk). */
#include "task.h"
#include "ext/mw/megawifi.h"

volatile bool md_mw_ready;
static volatile bool posted;

void md_pump(void)
{
    if (md_mw_ready) mw_process();
}

bool TSK_superPend(s16 wait)
{
    u32 t0 = md_tick();
    posted = FALSE;
    while (!posted) {
        md_pump();
        if (wait >= 0 && md_tick() - t0 >= (u32)wait) return TRUE;
    }
    posted = FALSE;
    return FALSE;
}

void TSK_superPost(bool force_ctx_sw)
{
    (void)force_ctx_sw;
    posted = TRUE;
}

void TSK_userSet(void (*task)(void)) { (void)task; }

/* src/net.c is built with -Dmw_init=md_mw_init: the driver isn't pumped while it resets. */
#undef mw_init
enum mw_err md_mw_init(u16 *cmd_buf, u16 buf_len)
{
    md_mw_ready = FALSE;
    enum mw_err e = mw_init(cmd_buf, buf_len);
    md_mw_ready = TRUE;
    return e;
}
