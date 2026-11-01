/* TL16C550D on the MegaWiFi Rev.C cart, 32X version (lode/mars/summary.md). Same API as SGDK's 16c550.h
 * (with sgdk/patches/0001: 20 MHz, 1.25 Mbaud), but in 32X mode the 68000 can only reach the UART with RV = 1,
 * so every register access goes through md/uart_ram.c (work RAM), never a plain pointer. */
#ifndef _16C550_H_
#define _16C550_H_

#include "types.h"
#include "ext/mw/mw-msg.h"

#define UART_CLK            20000000LU
#define UART_BR             1250000LU
#define UART_TX_FIFO_LEN    16
#define UART_BUFLEN         1460

/* register numbers (address UART_BASE + 2n) */
#define UR_RHR  0
#define UR_THR  0
#define UR_DLL  0
#define UR_IER  1
#define UR_DLM  1
#define UR_ISR  2
#define UR_FCR  2
#define UR_LCR  3
#define UR_MCR  4
#define UR_LSR  5
#define UR_MSR  6
#define UR_SPR  7

void uart_wr(u8 reg, u8 v);         /* md/uart_ram.c: RV = 1 around the access */
u8 uart_rd(u8 reg);

typedef struct {
    uint8_t IER;
    uint8_t FCR;
    uint8_t LCR;
    uint8_t MCR;
} UartShadow;
extern UartShadow sh;

#define UART_MCR__DTR       0x01
#define UART_MCR__RTS       0x02
#define UART_MCR__OUT1      0x04
#define UART_MCR__OUT2      0x08
#define UART_MSR__DSR       0x20
#define MW__RESET           UART_MCR__DTR
#define MW__PRG             UART_MCR__RTS
#define MW__DCD             UART_MSR__DSR

void uart_init(void);
bool uart_is_present(void);
bool uart_tx_ready(void);
bool uart_rx_ready(void);
void uart_putc(u8 c);
u8 uart_getc(void);
void uart_reset_fifos(void);
void uart_reset(void);
void uart_start(void);
void uart_line_sync(void);

#define uart_set(reg, val)      do { sh.reg = (val); uart_wr(UR_##reg, sh.reg); } while (0)
#define uart_get(reg)           (sh.reg)
#define uart_set_bits(reg, val) do { sh.reg |= (val); uart_wr(UR_##reg, sh.reg); } while (0)
#define uart_clr_bits(reg, val) do { sh.reg &= ~(val); uart_wr(UR_##reg, sh.reg); } while (0)

#endif
