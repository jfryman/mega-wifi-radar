/* TL16C550D (U7) register map on the MegaWiFi cart. Same map as SC16C550B:
 * the UART sits in the $A130xx (~TIME) area, odd bytes, cart A1..A3 -> A0..A2. */
#ifndef UART_H
#define UART_H

#include <stdint.h>

#define UART_BASE	0xA130C1UL
#define UREG(n)		(*(volatile uint8_t *)(UART_BASE + 2 * (n)))

#define U_RHR	UREG(0)	/* read,  DLAB=0 */
#define U_THR	UREG(0)	/* write, DLAB=0 */
#define U_DLL	UREG(0)	/* DLAB=1 */
#define U_IER	UREG(1)	/* DLAB=0 */
#define U_DLM	UREG(1)	/* DLAB=1 */
#define U_ISR	UREG(2)	/* read */
#define U_FCR	UREG(2)	/* write */
#define U_LCR	UREG(3)
#define U_MCR	UREG(4)
#define U_LSR	UREG(5)
#define U_MSR	UREG(6)
#define U_SPR	UREG(7)

/* Rev.C clock: Y1 = 20 MHz (TL16C550D RHB max at 3.3 V). Divisor 1 = 1.25 Mbaud. */
#define UART_CLK_HZ	20000000UL
#define LINK_DIVISOR	1

#define LSR_DR		0x01
#define LSR_OE		0x02
#define LSR_PE		0x04
#define LSR_FE		0x08
#define LSR_BI		0x10
#define LSR_THRE	0x20
#define LSR_TEMT	0x40
#define LSR_ERRS	(LSR_OE | LSR_PE | LSR_FE | LSR_BI)

/* MCR. On this board ~DTR drives ESP32 EN (bit set = ESP held in reset) and
 * ~RTS drives ESP32 GPIO9, a boot strap: keep RTS clear while EN is released. */
#define MCR_DTR		0x01
#define MCR_RTS		0x02
#define MCR_OUT1	0x04
#define MCR_OUT2	0x08
#define MCR_LOOP	0x10
#define MCR_AFE		0x20

#define MSR_CTS		0x10
#define MSR_DSR		0x20
#define MSR_RI		0x40
#define MSR_DCD		0x80

#define FCR_TRIG14_RESET 0xC7	/* FIFO on, clear RX+TX, RX trigger 14 */

/* Register numbers for uart_wr(). In 32X mode every write goes through uart_ram.c (RV = 1). */
#define R_THR	0
#define R_DLL	0
#define R_IER	1
#define R_DLM	1
#define R_FCR	2
#define R_LCR	3
#define R_MCR	4
#define R_RHR	0
#define R_ISR	2
#define R_LSR	5
#define R_MSR	6
#define R_SPR	7

extern uint8_t uart_rv_wr, uart_rv_rd;
void uart_wr(uint8_t reg, uint8_t v);
uint8_t uart_rd(uint8_t reg);
int uart_tx(const uint8_t *d, uint16_t n);
void uart_dump(uint8_t *out, uint8_t rv);
uint8_t uart_probe(uint8_t v, uint8_t wrv, uint8_t rrv);

#endif
