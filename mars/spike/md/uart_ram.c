/* UART access for 32X mode. With ADEN = 1 and RV = 0 the 32X never asserts /LWR on the cartridge
 * slot, so 68000 writes to odd addresses (all TL16C550D registers, $A130C1 + 2n) are lost. The
 * Rev.C UART's read strobe is the slot's ~OE, which nobody has measured under the 32X, so reads may
 * need RV = 1 too: uart_probe() tries every combination and uart_rv_wr / uart_rv_rd select one.
 * RV = 1 also unmaps the cartridge from $880000, where our code lives, so everything here runs from
 * work RAM (.ramcode, copied by boot.s with .data) and touches nothing in ROM while RV = 1.
 * The SH-2s' ROM reads wait until RV = 0. */
#include "uart.h"

#define RV	(*(volatile uint8_t *)0xA15107)	/* bit 0: RV */
#define RAMCODE	__attribute__((section(".ramcode"), noinline))

uint8_t uart_rv_wr = 1, uart_rv_rd = 1;	/* RV during writes / reads */

void RAMCODE uart_wr(uint8_t reg, uint8_t v)
{
	RV = uart_rv_wr;
	UREG(reg) = v;
	RV = 0;
}

uint8_t RAMCODE uart_rd(uint8_t reg)
{
	uint8_t v;
	RV = uart_rv_rd;
	v = UREG(reg);
	RV = 0;
	return v;
}

/* d must be in RAM. Sends n bytes, each once THRE is set; returns 1 if THRE stays low
 * for ~0.5 s (auto-CTS holding us off), else 0. */
int RAMCODE uart_tx(const uint8_t *d, uint16_t n)
{
	int err = 0;
	while (n--) {
		uint32_t spin = 0;
		RV = uart_rv_rd;
		while (!(U_LSR & LSR_THRE))
			if (++spin > 100000) {
				err = 1;
				goto out;
			}
		RV = uart_rv_wr;
		U_THR = *d++;
	}
out:
	RV = 0;
	return err;
}

/* Raw register dump with RV = rv for the reads (no writes). */
void RAMCODE uart_dump(uint8_t *out, uint8_t rv)
{
	uint8_t i;
	RV = rv;
	for (i = 0; i < 8; i++)
		out[i] = UREG(i);
	RV = 0;
}

/* Write v to SPR with RV = wrv, read it back with RV = rrv (another register read in between
 * defeats open-bus echo). */
uint8_t RAMCODE uart_probe(uint8_t v, uint8_t wrv, uint8_t rrv)
{
	uint8_t r;
	RV = wrv;
	U_SPR = v;
	RV = rrv;
	(void)U_LSR;
	r = U_SPR;
	RV = 0;
	return r;
}
