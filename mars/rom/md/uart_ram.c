/* The only code that touches the MegaWiFi UART (lode/plans/32x.md, rule 1): with ADEN = 1 and RV = 0 the 32X
 * drops the cartridge's write strobe and its reads return open bus, so each access sets RV = 1. RV = 1 also
 * unmaps the cartridge from $880000, where the rest of this program runs, so these live in work RAM
 * (.ramcode, copied with .data by boot.s) and touch nothing in ROM while RV = 1. */
#include "genesis.h"

#define RV          (*(volatile u8 *)0xA15107)      /* DREQ control, bit 0 */
#define UREG(n)     (*(volatile u8 *)(0xA130C1 + 2 * (n)))
#define RAMCODE     __attribute__((section(".ramcode"), noinline))

void RAMCODE uart_wr(u8 reg, u8 v)
{
    RV = 1;
    UREG(reg) = v;
    RV = 0;
}

u8 RAMCODE uart_rd(u8 reg)
{
    u8 v;
    RV = 1;
    v = UREG(reg);
    RV = 0;
    return v;
}
