/* Slave SH-2: the app. It waits for the 68000's link, learns what it serves, and runs src/main.c, the Genesis
 * ROM's own main loop, on the 32X implementations of its screens and feed (lode/mars/summary.md). */
#include "genesis.h"
#include "link_sh.h"
#include "shared.h"
#include "../link.h"

int main(bool hard);

void s_main(void)
{
    u8 r[64];
    u16 got = 0;

    lk_wait_ready();
    memset(r, 0, sizeof(r));
    lk_call(LK_HELLO, NULL, 0, r, sizeof(r) - 1, &got);
    sh.fake = r[0];
    sh.rom_checksum = r[2] << 8 | r[3];
    main(TRUE);
    for (;;) {}
}
