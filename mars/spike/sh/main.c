/* 32X spike, SH-2 side.
 * Master: 256-colour frame buffer behind the 68000's VDP text. A gradient fills the screen; the
 * bottom band shows the relay result from the 68000 (grey idle/pending, green ok, red failed,
 * yellow skipped) with the response size in digits, and a block that moves every flip.
 * Slave: reads the cart ROM through the cache-through window forever, so the 68000's UART
 * tests run with SH-2 traffic on the cart bus. */
#include <stdint.h>
#include "../shared.h"

#define REG16(a)	(*(volatile uint16_t *)(a))
#define ADAPTER		REG16(0x20004000)	/* bit 15 FM: SH-2s own the 32X VDP */
#define COMM(n)		REG16(0x20004020 + (n))
#define DISPMODE	REG16(0x20004100)
#define FBCTL		REG16(0x2000410A)
#define CRAM		((volatile uint16_t *)0x20004200)
#define FB		((volatile uint16_t *)0x24000000)	/* line table, then pixels */
#define ROM_NC		((volatile uint16_t *)0x22000000)	/* cart ROM, cache-through */

#define MODE_256	0x0001
#define LINES_224	0x0000
#define FBCTL_FS	0x0001
#define RGB(r, g, b)	((uint16_t)((r) | (g) << 5 | (b) << 10))	/* 5:5:5 */

#define W		320
#define H		224
#define LINE_WORDS	(W / 2)
#define PIX0		0x100		/* word offset of line 0 */
#define BAND_Y		196
#define BAND_H		24

enum { C_GRAD = 1, C_BAND_IDLE = 64, C_BAND_OK, C_BAND_FAIL, C_BAND_SKIP, C_WHITE, C_INK };

static uint16_t fb_cur;

static void flip(void)
{
	FBCTL = fb_cur ^ 1;
	while ((FBCTL & FBCTL_FS) == fb_cur)
		;
	fb_cur ^= 1;
}

static void hline(uint16_t y, uint16_t x0, uint16_t x1, uint8_t c)
{
	volatile uint16_t *p = FB + PIX0 + y * LINE_WORDS + x0 / 2;
	uint16_t w = (uint16_t)(c << 8 | c), x;
	for (x = x0; x < x1; x += 2)
		*p++ = w;
}

/* 3x5 digits, bit 2 = left column */
static const uint8_t digit[10][5] = {
	{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
	{7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

static void draw_digit(uint16_t x, uint16_t y, uint8_t d)
{
	uint8_t r, c;
	for (r = 0; r < 5; r++)
		for (c = 0; c < 3; c++)
			if (digit[d][r] & (4 >> c)) {
				uint8_t k;
				for (k = 0; k < 4; k++)
					hline(y + r * 4 + k, x + c * 4, x + c * 4 + 4, C_INK);
			}
}

static void draw_number(uint16_t x, uint16_t y, uint16_t v)
{
	char buf[5];
	int8_t n = 0;
	do {
		buf[n++] = (char)(v % 10);
		v /= 10;
	} while (v && n < 5);
	while (n--) {
		draw_digit(x, y, (uint8_t)buf[n]);
		x += 16;
	}
}

static void draw_frame(uint16_t frame)
{
	uint16_t state = COMM(COMM_NET_STATE), y, bx = (uint16_t)(frame * 2 % (W - 16)) & ~1u;
	uint8_t bg = state == NET_OK ? C_BAND_OK : state == NET_FAILED ? C_BAND_FAIL :
		     state == NET_SKIPPED ? C_BAND_SKIP : C_BAND_IDLE;
	for (y = BAND_Y; y < BAND_Y + BAND_H; y++) {
		hline(y, 0, W, bg);
		if (y >= BAND_Y + 2 && y < BAND_Y + 22)
			hline(y, bx, bx + 16, C_WHITE);
	}
	if (state == NET_OK || state == NET_FAILED)
		draw_number(200, BAND_Y + 2, COMM(COMM_NET_BYTES));
}

static void setup_buffer(void)
{
	uint16_t y;
	for (y = 0; y < H; y++)
		FB[y] = PIX0 + y * LINE_WORDS;
	for (y = 0; y < H; y++)
		hline(y, 0, W, (uint8_t)(C_GRAD + y * 32 / H));
}

void m_main(void)
{
	uint16_t i, frame = 0;
	while (!(ADAPTER & 0x8000))
		;
	DISPMODE = LINES_224 | MODE_256;	/* VDP layers in front where they aren't backdrop */
	for (i = 0; i < 32; i++)
		CRAM[C_GRAD + i] = RGB(0, i / 4, 4 + i / 2);	/* dark blue gradient */
	CRAM[C_BAND_IDLE] = RGB(8, 8, 8);
	CRAM[C_BAND_OK] = RGB(0, 18, 4);
	CRAM[C_BAND_FAIL] = RGB(20, 2, 2);
	CRAM[C_BAND_SKIP] = RGB(18, 16, 0);
	CRAM[C_WHITE] = RGB(31, 31, 31);
	CRAM[C_INK] = RGB(31, 31, 31);
	fb_cur = FBCTL & FBCTL_FS;
	setup_buffer();
	flip();
	setup_buffer();
	for (;;) {
		draw_frame(frame);
		flip();
		COMM(COMM_MASTER_FRAMES) = ++frame;
	}
}

void s_main(void)
{
	uint16_t passes = 0;
	volatile uint32_t sink;
	for (;;) {
		uint32_t i, sum = 0;
		for (i = 0; i < 0x8000; i++)	/* first 64 KiB of the cart */
			sum += ROM_NC[i];
		sink = sum;
		COMM(COMM_SLAVE_PASSES) = ++passes;
	}
	(void)sink;
}
