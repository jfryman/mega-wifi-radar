/* SGDK's 16c550.c (with sgdk/patches/0001), for the 32X: register access through uart_ram.c. */
#include "ext/mw/16c550.h"
#include "task.h"

#define DIV_ROUND(n, d)  ((((n) * 2 / (d)) + 1) / 2)
#define UART_DIV         DIV_ROUND(UART_CLK, 16 * UART_BR)

UartShadow sh;

void uart_init(void)
{
    uart_wr(UR_LCR, 0x83);              /* DLAB for the divisor */
    uart_wr(UR_DLM, UART_DIV >> 8);
    uart_wr(UR_DLL, UART_DIV & 0xFF);
    uart_set(LCR, 0x03);
    uart_set(MCR, 0x22);                /* auto RTS/CTS */
    uart_wr(UR_FCR, 0xC1);
    uart_set(FCR, 0xC7);                /* FIFOs on and reset, RX trigger 14 */
    uart_set(IER, 0x00);
    uart_line_sync();
    uart_reset();
    uart_clr_bits(MCR, MW__PRG);
    uart_start();
}

void uart_reset(void) { uart_set_bits(MCR, MW__RESET); }

void uart_start(void)
{
    TSK_superPend(MW_MS_TO_FRAMES(30));
    uart_clr_bits(MCR, MW__RESET);
    TSK_superPend(MW_MS_TO_FRAMES(1000));
    uart_set_bits(MCR, MW__PRG);
    uart_reset_fifos();
}

bool uart_is_present(void)
{
    uart_wr(UR_SPR, 0x55);
    bool a = uart_rd(UR_SPR) == 0x55;
    uart_wr(UR_SPR, 0xAA);
    return a && uart_rd(UR_SPR) == 0xAA;
}

bool uart_tx_ready(void) { return uart_rd(UR_LSR) & 0x20; }
bool uart_rx_ready(void) { return uart_rd(UR_LSR) & 0x01; }
void uart_putc(u8 c) { uart_wr(UR_THR, c); }
u8 uart_getc(void) { return uart_rd(UR_RHR); }
void uart_reset_fifos(void) { uart_set_bits(FCR, 0x07); }

void uart_line_sync(void)
{
    for (int i = 0; i < 256; i++)
        if (uart_tx_ready()) uart_putc(0x55);
}
