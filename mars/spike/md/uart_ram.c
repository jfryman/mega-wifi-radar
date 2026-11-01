/* UART writes for 32X mode. With ADEN = 1 and RV = 0 the 32X never asserts /LWR on the cartridge
 * slot, so 68000 writes to odd addresses (all TL16C550D registers, $A130C1 + 2n) are lost; /TIME
 * and reads are fine. RV = 1 restores /LWR but also unmaps the cartridge from $880000, where our
 * code lives, so these routines run from work RAM (.ramcode, copied by boot.s with .data) and
 * touch nothing in ROM while RV = 1. The SH-2s' ROM reads wait until RV = 0. */
#include "uart.h"

#define RV	(*(volatile uint8_t *)0xA15107)	/* bit 0: RV */
#define RAMCODE	__attribute__((section(".ramcode"), noinline))

void RAMCODE uart_wr(uint8_t reg, uint8_t v)
{
	RV = 1;
	UREG(reg) = v;
	RV = 0;
}

/* d must be in RAM. Sends n bytes, each once THRE is set; returns 1 if THRE stays low
 * for ~0.5 s (auto-CTS holding us off), else 0. */
int RAMCODE uart_tx(const uint8_t *d, uint16_t n)
{
	int err = 0;
	RV = 1;
	while (n--) {
		uint32_t spin = 0;
		while (!(U_LSR & LSR_THRE))
			if (++spin > 100000) {
				err = 1;
				goto out;
			}
		U_THR = *d++;
	}
out:
	RV = 0;
	return err;
}
