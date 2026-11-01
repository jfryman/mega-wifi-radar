/* 32X spike, 68000 side: does MegaWiFi Rev.C work with the 32X adapter enabled?
 *
 * Runs at $88xxxx with ADEN = 1 (the 32X mapping) and, in dependency order:
 *   1 32X adapter   - ADEN set, MARS ID, this code linked in the $88xxxx window
 *   2 SH-2 master   - its frame counter moves (COMM8): it flips the 32X frame buffer
 *   3 SH-2 slave    - its ROM-read pass counter moves (COMM10): cart-bus traffic during the UART tests
 *   4 UART SPR      - the go/no-go: the TL16C550D at $A130C1 (/TIME) answers through the 32X
 *   5 reg decode    - A0..A2 decode and DLAB through the 32X
 *   6 loopback 8K   - 8 KiB through the UART's internal loop at 1.25 Mbaud
 *   7 ESP32 version - ESP reset via DTR, LSD/MW_CMD_VERSION
 *   8 echo x300     - MW_CMD_ECHO, sizes 1..1024, while the slave SH-2 reads ROM
 *   9 Wi-Fi join    - default AP slot, MW_CMD_AP_JOIN, wait for READY, show the IP
 *  10 relay GET     - TCP to RELAY_HOST:RELAY_PORT (build-time), GET /healthz, result to the SH-2
 * The UART, LSD and test code follows jfryman/mw-cart bringup-rom. Every UART write goes through
 * uart_ram.c (RV = 1, run from RAM): with RV = 0 the 32X drops odd-address writes. START reruns everything.
 */
#include <stdint.h>
#include "vdp.h"
#include "uart.h"
#include "../shared.h"

#define NAME_X	3
#define RES_X	18
#define INFO_X	23
#define FIRST_ROW 3

#define REG8(a)		(*(volatile uint8_t *)(a))
#define REG16(a)	(*(volatile uint16_t *)(a))
#define REG32(a)	(*(volatile uint32_t *)(a))
#define MARS_ID		REG32(0xA130EC)
#define MARS_COMM(n)	REG16(0xA15120 + (n))

/* ------------------------------------------------------------------ helpers */

static uint32_t now(void) { return vdp_frames(); }

static void wait_frames(uint32_t n)
{
	uint32_t t = now();
	while (now() - t < n)
		;
}

static int start_pressed(void)
{
	uint8_t v;
	REG8(0xA10009) = 0x40;
	REG8(0xA10003) = 0x00;
	__asm__ volatile("nop\n nop");
	v = REG8(0xA10003);
	REG8(0xA10003) = 0x40;
	return !(v & 0x20);
}

static uint16_t lfsr;
static uint8_t rnd(void)
{
	lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
	return (uint8_t)lfsr;
}

static void row_name(uint8_t row, const char *num, const char *name)
{
	txt_clear_row(row);
	txt_put(0, row, COL_WHITE, num);
	txt_put(NAME_X, row, COL_WHITE, name);
	txt_put(RES_X, row, COL_YELLOW, "....");
}

static int verdict(uint8_t row, int ok)
{
	txt_put(RES_X, row, ok ? COL_GREEN : COL_RED, ok ? "PASS" : "FAIL");
	return ok;
}

static void skip(uint8_t row, const char *why)
{
	txt_put(RES_X, row, COL_YELLOW, "SKIP");
	txt_put(INFO_X, row, COL_YELLOW, why);
}

static void wr_detail(uint8_t row, uint8_t w, uint8_t r)
{
	txt_put(INFO_X, row, COL_RED, "W");
	txt_hex(INFO_X + 1, row, COL_RED, w, 2);
	txt_put(INFO_X + 4, row, COL_RED, "R");
	txt_hex(INFO_X + 5, row, COL_RED, r, 2);
}

static void uart_setup(uint16_t divisor, uint8_t mcr)
{
	uart_wr(R_LCR, 0x83);		/* DLAB=1, 8N1 */
	uart_wr(R_DLL, divisor & 0xFF);
	uart_wr(R_DLM, divisor >> 8);
	uart_wr(R_LCR, 0x03);		/* DLAB=0, 8N1 */
	uart_wr(R_IER, 0x00);
	uart_wr(R_FCR, FCR_TRIG14_RESET);
	uart_wr(R_MCR, mcr);
}

static void rx_flush(void)
{
	uint16_t n = 0;
	while ((U_LSR & LSR_DR) && n++ < 64)
		(void)U_RHR;
}

static void sh_net(uint16_t state, uint16_t bytes)
{
	MARS_COMM(COMM_NET_BYTES) = bytes;
	MARS_COMM(COMM_NET_STATE) = state;
}

/* ------------------------------------------------------------------ 32X */

static int t_adapter(uint8_t row)
{
	uint32_t pc = (uint32_t)&t_adapter;
	/* Running from $88xxxx is the proof that ADEN is on. A15100 is shown for information only,
	 * read as two bytes: MAME 0.287 returns 0000 for a word read on the 68000 side. */
	uint16_t ac = (uint16_t)(REG8(0xA15100) << 8 | REG8(0xA15101));
	int ok = MARS_ID == 0x4D415253 && pc >= 0x880000 && pc < 0x900000;
	row_name(row, "1", "32X adapter");
	txt_put(INFO_X, row, COL_WHITE, "A15100=");
	txt_hex(INFO_X + 7, row, COL_WHITE, ac, 4);
	txt_put(3, row + 1, COL_WHITE, "code at");
	txt_hex(11, row + 1, COL_WHITE, pc, 8);
	txt_put(21, row + 1, COL_WHITE, MARS_ID == 0x4D415253 ? "MARS ID ok" : "no MARS ID");
	return verdict(row, ok);
}

/* A counter written by an SH-2 must move within ~1 s. */
static int t_counter(uint8_t row, const char *num, const char *name, uint8_t comm)
{
	uint16_t a = MARS_COMM(comm), b = a;
	uint32_t t0 = now();
	row_name(row, num, name);
	while (now() - t0 < 60 && (b = MARS_COMM(comm)) == a)
		;
	txt_put(INFO_X, row, COL_WHITE, "count");
	txt_hex(INFO_X + 6, row, b != a ? COL_WHITE : COL_RED, b, 4);
	return verdict(row, b != a);
}

/* ------------------------------------------------------------------ UART */

static int t_spr(uint8_t row)
{
	static const uint8_t pat[] = { 0x00, 0xFF, 0x55, 0xAA, 0x01, 0x02, 0x04, 0x08,
		0x10, 0x20, 0x40, 0x80, 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F };
	uint8_t i;
	row_name(row, "4", "UART SPR");
	for (i = 0; i < sizeof(pat); i++) {
		uint8_t r;
		uart_wr(R_SPR, pat[i]);
		(void)U_LSR;	/* different register in between: defeats open-bus echo */
		r = U_SPR;
		if (r != pat[i]) {
			wr_detail(row, pat[i], r);
			return verdict(row, 0);
		}
	}
	txt_put(INFO_X, row, COL_WHITE, "/TIME ok");
	return verdict(row, 1);
}

static int check_reg(uint8_t row, const char *name, uint8_t want, uint8_t got)
{
	if (want == got)
		return 1;
	txt_put(INFO_X, row, COL_RED, name);
	wr_detail(row + 1, want, got);
	return 0;
}

static int t_decode(uint8_t row)
{
	/* MCR patterns use only OUT1/OUT2/LOOP so the ESP32 EN/GPIO9 pins never move */
	static const uint8_t v[2][4] = {
		/* SPR   LCR   IER   MCR */
		{ 0x3C, 0x1B, 0x05, 0x0C },
		{ 0xC3, 0x24, 0x0A, 0x14 },
	};
	uint8_t i;
	int ok = 1;
	row_name(row, "5", "reg decode");
	for (i = 0; i < 2 && ok; i++) {
		uart_wr(R_SPR, v[i][0]);
		uart_wr(R_LCR, v[i][1]);
		uart_wr(R_IER, v[i][2]);
		uart_wr(R_MCR, v[i][3]);
		ok = check_reg(row, "SPR", v[i][0], U_SPR) &&
		     check_reg(row, "LCR", v[i][1], U_LCR) &&
		     check_reg(row, "IER", v[i][2], U_IER & 0x0F) &&
		     check_reg(row, "MCR", v[i][3], U_MCR & 0x1F);
	}
	if (ok) {
		uart_wr(R_LCR, 0x83);
		uart_wr(R_DLL, 0x5A);
		uart_wr(R_DLM, 0xA5);
		ok = check_reg(row, "DLL", 0x5A, U_DLL) && check_reg(row, "DLM", 0xA5, U_DLM);
		uart_wr(R_LCR, 0x03);
		ok = ok && check_reg(row, "IER", 0x0A, U_IER & 0x0F);
	}
	uart_wr(R_IER, 0);
	uart_wr(R_MCR, 0);
	uart_wr(R_LCR, 0x03);
	return verdict(row, ok);
}

static int t_loop_data(uint8_t row)
{
	const uint16_t total = 8192;
	uint16_t sent = 0, rcvd = 0;
	uint16_t tx_lfsr, rx_lfsr = 0x1D0F;
	row_name(row, "6", "loopback 8K");
	uart_setup(LINK_DIVISOR, MCR_LOOP);
	rx_flush();
	lfsr = 0x1D0F;
	tx_lfsr = lfsr;
	while (rcvd < total) {
		uint8_t burst[8], k = 0;
		uint32_t t0;
		lfsr = tx_lfsr;
		while (k < 8 && sent + k < total)
			burst[k++] = rnd();
		if (uart_tx(burst, k)) {
			txt_put(INFO_X, row, COL_RED, "TX stuck");
			return verdict(row, 0);
		}
		sent += k;
		tx_lfsr = lfsr;
		lfsr = rx_lfsr;
		t0 = now();
		while (rcvd < sent) {
			uint8_t lsr = U_LSR;
			if (lsr & LSR_DR) {
				uint8_t want = rnd(), got = U_RHR;
				if ((lsr & LSR_ERRS) || got != want) {
					wr_detail(row, want, got);
					return verdict(row, 0);
				}
				rcvd++;
			} else if (now() - t0 > 3) {
				txt_put(INFO_X, row, COL_RED, "RX stall");
				return verdict(row, 0);
			}
		}
		rx_lfsr = lfsr;
	}
	uart_wr(R_MCR, 0);
	return verdict(row, 1);
}

/* ------------------------------------------------------------- LSD / MW */

#define LSD_STX_ETX	0x7E
#define LSD_MAX		1460
#define MW_CMD_OK	0
#define MW_CMD_VERSION	1
#define MW_CMD_ECHO	2
#define MW_CMD_IP_CURRENT 6
#define MW_CMD_DEF_AP_CFG_GET 11
#define MW_CMD_AP_JOIN	12
#define MW_CMD_TCP_CON	14
#define MW_CMD_CLOSE	17
#define MW_CMD_SYS_STAT	30
#define MW_ST_READY	4
#define ECHO_MAX	1024
#define TCP_CH		1

static uint8_t txbuf[4 + LSD_MAX] __attribute__((aligned(2)));
static uint8_t rxbuf[4 + LSD_MAX + 16] __attribute__((aligned(2)));

enum lsd_err { LSD_OK = 0, LSD_TX_TMO, LSD_RX_TMO, LSD_LSR, LSD_FRAMING, LSD_TOOBIG, LSD_MISMATCH, LSD_REFUSED };
static const char *const err_txt[] = { "ok", "CTS held", "no reply", "LSR err", "framing", "too big",
	"mismatch", "refused" };
static uint8_t last_lsr;

static enum lsd_err lsd_send(uint8_t ch, const uint8_t *d, uint16_t len)
{
	uint8_t hdr[3], etx = LSD_STX_ETX;	/* on the stack: uart_tx needs RAM */
	hdr[0] = LSD_STX_ETX;
	hdr[1] = (ch << 4) | (len >> 8);
	hdr[2] = len & 0xFF;
	if (uart_tx(hdr, 3) || uart_tx(d, len) || uart_tx(&etx, 1))
		return LSD_TX_TMO;	/* auto-CTS held off for ~0.5 s */
	return LSD_OK;
}

static uint32_t rx_deadline;	/* absolute frame count bounding a whole lsd_recv() */

static enum lsd_err rx_byte(uint8_t *b, uint32_t tmo)
{
	uint32_t t0 = now();
	uint8_t lsr;
	while (!((lsr = U_LSR) & LSR_DR))
		if (now() - t0 > tmo)
			return LSD_RX_TMO;
	if ((int32_t)(now() - rx_deadline) > 0)
		return LSD_RX_TMO;
	*b = U_RHR;
	if (lsr & LSR_ERRS) {
		last_lsr = lsr;
		return LSD_LSR;
	}
	return LSD_OK;
}

static enum lsd_err lsd_recv(uint8_t *ch, uint8_t *d, uint16_t max, uint16_t *len, uint32_t tmo)
{
	enum lsd_err e;
	uint8_t b, hi;
	uint16_t i, n;
	rx_deadline = now() + 2 * tmo;
	do {			/* skip noise until STX */
		if ((e = rx_byte(&b, tmo)))
			return e;
	} while (b != LSD_STX_ETX);
	do {			/* back-to-back 0x7E: previous ETX + STX */
		if ((e = rx_byte(&hi, tmo)))
			return e;
	} while (hi == LSD_STX_ETX);
	if ((e = rx_byte(&b, tmo)))
		return e;
	n = ((uint16_t)(hi & 0x0F) << 8) | b;
	if (n > max)
		return LSD_TOOBIG;
	for (i = 0; i < n; i++)
		if ((e = rx_byte(&d[i], tmo)))
			return e;
	if ((e = rx_byte(&b, tmo)))
		return e;
	*ch = hi >> 4;
	*len = n;
	return b == LSD_STX_ETX ? LSD_OK : LSD_FRAMING;
}

/* One channel-0 command: txbuf[4..] holds len data bytes. Frames on other channels
 * (socket data) are dropped. On success rxbuf[4..] holds the reply data, *rlen its length. */
static enum lsd_err mw_cmd(uint8_t cmd, uint16_t len, uint16_t *rlen, uint32_t tmo)
{
	enum lsd_err e;
	uint8_t ch;
	uint16_t n = 0;
	txbuf[0] = 0; txbuf[1] = cmd; txbuf[2] = len >> 8; txbuf[3] = len & 0xFF;
	if ((e = lsd_send(0, txbuf, 4 + len)))
		return e;
	do {
		if ((e = lsd_recv(&ch, rxbuf, sizeof(rxbuf), &n, tmo)))
			return e;
	} while (ch != 0);
	if (n < 4)
		return LSD_FRAMING;
	if (rxbuf[0] != 0 || rxbuf[1] != MW_CMD_OK)
		return LSD_REFUSED;
	if (rlen)
		*rlen = n - 4;
	return LSD_OK;
}

static void esp_boot(void)
{
	/* SGDK uart_init/uart_reset/uart_start sequence: hold EN low with GPIO9
	 * (RTS) high, release, let it boot, then enable RTS for auto flow. */
	uart_setup(LINK_DIVISOR, MCR_AFE | MCR_DTR);
	wait_frames(3);
	uart_wr(R_MCR, MCR_AFE);
	wait_frames(60);
	uart_wr(R_FCR, FCR_TRIG14_RESET);	/* drop the ROM bootloader's 115200 baud chatter */
	rx_flush();
	uart_wr(R_MCR, MCR_AFE | MCR_RTS);
}

static void show_err(uint8_t row, enum lsd_err e)
{
	txt_put(INFO_X, row, COL_RED, err_txt[e]);
	if (e == LSD_LSR)
		txt_hex(INFO_X + 8, row, COL_RED, last_lsr, 2);
}

static int t_version(uint8_t row)
{
	uint16_t len = 0;
	enum lsd_err e;
	row_name(row, "7", "ESP32 version");
	esp_boot();
	e = mw_cmd(MW_CMD_VERSION, 0, &len, 60);
	if (!e && len < 3)
		e = LSD_FRAMING;
	if (e) {
		show_err(row, e);
		if (e == LSD_RX_TMO)
			txt_put(3, row + 1, COL_YELLOW, "fw must use LSD_UART_BR 1250000");
		return verdict(row, 0);
	}
	{
		uint8_t x = INFO_X;
		x += txt_dec(x, row, COL_WHITE, rxbuf[4]);
		txt_put(x++, row, COL_WHITE, ".");
		x += txt_dec(x, row, COL_WHITE, rxbuf[5]);
		txt_put(x++, row, COL_WHITE, ".");
		txt_dec(x, row, COL_WHITE, rxbuf[6]);
		rxbuf[4u + len < sizeof(rxbuf) ? 4u + len : sizeof(rxbuf) - 1] = 0;
		txt_put(3, row + 1, COL_WHITE, "variant:");
		txt_put(12, row + 1, COL_WHITE, (const char *)rxbuf + 7);
	}
	return verdict(row, 1);
}

static int t_echo(uint8_t row, uint16_t rounds)
{
	static const uint16_t sizes[] = { 1, 16, 64, 255, 256, 512, 1000, 1024 };
	uint16_t r, ok = 0, bad = 0;
	enum lsd_err last = LSD_OK;
	row_name(row, "8", "echo x300");
	lfsr = 0xBEEF;
	for (r = 0; r < rounds; r++) {
		uint16_t n = sizes[r % (sizeof(sizes) / sizeof(sizes[0]))], i, len = 0;
		enum lsd_err e;
		for (i = 0; i < n; i++)
			txbuf[4 + i] = rnd();
		e = mw_cmd(MW_CMD_ECHO, n, &len, 60);
		if (!e && len != n)
			e = LSD_MISMATCH;
		for (i = 0; !e && i < n; i++)
			if (rxbuf[4 + i] != txbuf[4 + i])
				e = LSD_MISMATCH;
		if (e) {
			bad++;
			last = e;
			esp_boot();	/* resync after any failure */
		} else {
			ok++;
		}
		if ((r & 15) == 15 || r == rounds - 1) {
			txt_put(INFO_X, row, COL_WHITE, "ok");
			txt_dec(INFO_X + 3, row, COL_WHITE, ok);
			txt_put(INFO_X + 8, row, COL_WHITE, "bad");
			txt_dec(INFO_X + 12, row, bad ? COL_RED : COL_GREEN, bad);
		}
	}
	if (bad) {
		txt_clear_row(row + 1);
		txt_put(3, row + 1, COL_RED, "last error:");
		txt_put(15, row + 1, COL_RED, err_txt[last]);
	}
	return verdict(row, bad == 0);
}

static int t_wifi(uint8_t row)
{
	uint16_t len = 0;
	uint8_t slot = 0;
	enum lsd_err e;
	uint32_t t0;
	row_name(row, "9", "Wi-Fi join");
	if (!mw_cmd(MW_CMD_DEF_AP_CFG_GET, 0, &len, 60) && len >= 1 && rxbuf[4] < 3)
		slot = rxbuf[4];
	txt_put(INFO_X, row, COL_WHITE, "slot");
	txt_dec(INFO_X + 5, row, COL_WHITE, slot);
	txbuf[4] = slot;
	if ((e = mw_cmd(MW_CMD_AP_JOIN, 1, &len, 300))) {
		show_err(row, e);
		return verdict(row, 0);
	}
	/* the module stops answering for ~3 s after AP_JOIN (see SGDK mw_ap_assoc_wait) */
	wait_frames(210);
	t0 = now();
	for (;;) {
		e = mw_cmd(MW_CMD_SYS_STAT, 0, &len, 60);
		if (!e && len >= 1 && rxbuf[4] >= MW_ST_READY)
			break;
		if (now() - t0 > 20 * 60) {
			txt_put(INFO_X, row, COL_RED, e ? err_txt[e] : "no AP    ");
			return verdict(row, 0);
		}
		wait_frames(30);
	}
	txt_put(INFO_X, row, COL_WHITE, "        ");
	if (!mw_cmd(MW_CMD_IP_CURRENT, 0, &len, 60) && len >= 4) {
		uint8_t x = INFO_X, i;
		for (i = 0; i < 4; i++) {
			x += txt_dec(x, row, COL_WHITE, rxbuf[4 + i]);
			if (i < 3)
				txt_put(x++, row, COL_WHITE, ".");
		}
	}
	return verdict(row, 1);
}

#ifndef RELAY_HOST
#define RELAY_HOST ""
#endif
#ifndef RELAY_PORT
#define RELAY_PORT "8080"
#endif

static void cat(char **p, const char *s)
{
	while (*s)
		*(*p)++ = *s++;
}

static int t_relay(uint8_t row)
{
	static const char host[] = RELAY_HOST, port[] = RELAY_PORT;
	enum lsd_err e;
	uint16_t len = 0, total = 0, i;
	uint8_t ch;
	char *p, first[32];
	uint8_t nfirst = 0;
	uint32_t t0;
	row_name(row, "10", "relay GET");
	txt_clear_row(row + 1);
	if (!host[0]) {
		skip(row, "no RELAY=");
		txt_put(3, row + 1, COL_YELLOW, "make mars-spike RELAY=host:port");
		sh_net(NET_SKIPPED, 0);
		return 1;
	}
	sh_net(NET_PENDING, 0);
	/* struct mw_msg_in_addr: dst_port[6], src_port[6], channel, dst_addr string */
	for (i = 0; i < 13; i++)
		txbuf[4 + i] = 0;
	for (i = 0; i < 5 && port[i]; i++)
		txbuf[4 + i] = port[i];
	txbuf[4 + 12] = TCP_CH;
	for (i = 0; host[i]; i++)
		txbuf[4 + 13 + i] = host[i];
	txbuf[4 + 13 + i] = 0;
	if ((e = mw_cmd(MW_CMD_TCP_CON, 13 + i + 1, &len, 600))) {
		show_err(row, e);
		txt_put(3, row + 1, COL_RED, host);
		sh_net(NET_FAILED, 0);
		return verdict(row, 0);
	}
	p = (char *)txbuf;
	cat(&p, "GET /healthz HTTP/1.0\r\nHost: ");
	cat(&p, host);
	cat(&p, "\r\nConnection: close\r\n\r\n");
	if ((e = lsd_send(TCP_CH, txbuf, (uint16_t)(p - (char *)txbuf)))) {
		show_err(row, e);
		sh_net(NET_FAILED, 0);
		return verdict(row, 0);
	}
	/* read the response until the peer closes (empty frame), 3 s of silence, or 10 s total */
	t0 = now();
	while (now() - t0 < 600) {
		e = lsd_recv(&ch, rxbuf, sizeof(rxbuf), &len, total ? 180 : 300);
		if (e)
			break;
		if (ch != TCP_CH)
			continue;
		if (len == 0)
			break;
		for (i = 0; i < len && nfirst < sizeof(first) - 1; i++) {
			if (rxbuf[i] == '\r' || rxbuf[i] == '\n')
				nfirst = sizeof(first) - 1;	/* first line only */
			else
				first[nfirst++] = rxbuf[i];
		}
		total += len;
	}
	first[nfirst] = 0;
	txbuf[4] = TCP_CH;
	(void)mw_cmd(MW_CMD_CLOSE, 1, &len, 60);
	txt_put(INFO_X, row, total ? COL_WHITE : COL_RED, "bytes");
	txt_dec(INFO_X + 6, row, total ? COL_WHITE : COL_RED, total);
	txt_put(3, row + 1, COL_WHITE, nfirst ? first : "(no response)");
	{
		int ok = nfirst >= 12 && first[9] == '2' && first[10] == '0' && first[11] == '0';
		sh_net(ok ? NET_OK : NET_FAILED, total);
		return verdict(row, ok);
	}
}

/* ------------------------------------------------------------------ main */

int main(void)
{
	vdp_init();
	for (;;) {
		uint8_t r;
		int bus_ok, mw_ok = 0;
		for (r = 0; r < TXT_ROWS; r++)
			txt_clear_row(r);
		sh_net(NET_IDLE, 0);
		txt_put(1, 0, COL_YELLOW, "mega_wifi_radar 32X spike");
		txt_put(1, 1, COL_WHITE, "MegaWiFi Rev.C with ADEN = 1");

		r = FIRST_ROW;
		t_adapter(r); r += 2;
		t_counter(r++, "2", "SH-2 master", COMM_MASTER_FRAMES);
		t_counter(r++, "3", "SH-2 slave", COMM_SLAVE_PASSES);
		bus_ok = t_spr(r++);
		bus_ok &= t_decode(r); r += 2;
		bus_ok &= t_loop_data(r++);
		if (bus_ok && t_version(r)) {
			r += 2;
			mw_ok = t_echo(r, 300); r += 2;
			if (mw_ok && t_wifi(r++))
				mw_ok = t_relay(r);
			else
				sh_net(NET_FAILED, 0);
		}
		txt_clear_row(22);
		txt_put(1, 22, bus_ok ? (mw_ok ? COL_GREEN : COL_YELLOW) : COL_RED,
			!bus_ok ? "UART not reachable in 32X mode" :
			mw_ok ? "MegaWiFi works in 32X mode" : "UART ok, MegaWiFi checks failed");
		txt_put(1, 23, COL_YELLOW, "START = rerun");
		while (!start_pressed())
			(void)now();
		while (start_pressed())
			(void)now();
	}
}
