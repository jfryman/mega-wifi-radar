#include "vdp.h"
#include "font.h"

#define VDP_DATA (*(volatile uint16_t *)0xC00000)
#define VDP_CTRL_W (*(volatile uint16_t *)0xC00004)
#define VDP_CTRL_L (*(volatile uint32_t *)0xC00004)

#define PLANE_A 0xC000
#define FONT_VRAM 0x0400	/* tile 32 == ASCII 32 */

static const uint8_t regs[] = {
	0x04,	/* 0: no HINT */
	0x44,	/* 1: display on, mode 5, no VINT (interrupts stay masked) */
	0x30,	/* 2: plane A at 0xC000 */
	0x3C,	/* 3: window at 0xF000 */
	0x07,	/* 4: plane B at 0xE000 */
	0x6C,	/* 5: sprites at 0xD800 */
	0x00, 0x00,	/* 6, 7: backdrop = palette 0 colour 0 */
	0x00, 0x00,
	0xFF,	/* 10: HINT counter */
	0x00,	/* 11: full-screen scroll */
	0x81,	/* 12: H40 */
	0x37,	/* 13: hscroll at 0xDC00 */
	0x00,
	0x02,	/* 15: autoincrement 2 */
	0x01,	/* 16: 64x32 planes */
	0x00, 0x00
};

/* colour 1 of palettes 0..3: white, green, red, yellow (BGR 0x0BGR) */
static const uint16_t fg[4] = { 0x0EEE, 0x00E0, 0x000E, 0x00EE };

static void vram_wr(uint16_t addr)
{
	VDP_CTRL_L = 0x40000000UL | ((uint32_t)(addr & 0x3FFF) << 16) | (addr >> 14);
}

void vdp_init(void)
{
	uint16_t i;
	(void)VDP_CTRL_W;
	for (i = 0; i < sizeof(regs); i++)
		VDP_CTRL_W = 0x8000 | (i << 8) | regs[i];

	vram_wr(0);
	for (i = 0; i < 0x8000; i++)
		VDP_DATA = 0;
	VDP_CTRL_L = 0x40000010UL;		/* VSRAM */
	for (i = 0; i < 40; i++)
		VDP_DATA = 0;
	VDP_CTRL_L = 0xC0000000UL;		/* CRAM */
	for (i = 0; i < 64; i++)
		VDP_DATA = (i & 15) == 1 ? fg[i >> 4] : 0;

	/* expand the 1bpp font to 4bpp tiles, pixel value 1 = colour 1 */
	vram_wr(FONT_VRAM);
	for (i = 0; i < FONT_NCHARS * 2; i++) {
		uint32_t rows = font[i];
		uint8_t r, b;
		for (r = 0; r < 4; r++) {
			uint32_t out = 0;
			for (b = 0; b < 8; b++) {
				out = (out << 4) | (rows & 1);
				rows >>= 1;
			}
			VDP_DATA = out >> 16;
			VDP_DATA = out;
		}
	}
}

int vdp_is_pal(void)
{
	return VDP_CTRL_W & 1;
}

/* Frame counter without interrupts: counts rising edges of the VDP's vblank flag.
 * Callers poll it in every wait loop, which is far more often than once per vblank. */
uint32_t vdp_frames(void)
{
	static uint32_t frames;
	static uint8_t in_vb;
	uint8_t vb = (VDP_CTRL_W & 8) != 0;
	if (vb && !in_vb)
		frames++;
	in_vb = vb;
	return frames;
}

static void cell(uint8_t x, uint8_t y)
{
	vram_wr(PLANE_A + ((uint16_t)y * 64 + x) * 2);
}

void txt_clear_row(uint8_t y)
{
	uint8_t i;
	cell(0, y);
	for (i = 0; i < TXT_COLS; i++)
		VDP_DATA = 0;
}

void txt_put(uint8_t x, uint8_t y, enum txt_col c, const char *s)
{
	cell(x, y);
	while (*s && x++ < TXT_COLS) {
		uint8_t ch = (uint8_t)*s++;
		if (ch < 32 || ch > 127)
			ch = '?';
		VDP_DATA = ((uint16_t)c << 13) | ch;
	}
}

uint8_t txt_hex(uint8_t x, uint8_t y, enum txt_col c, uint32_t v, uint8_t digits)
{
	char buf[9];
	uint8_t i;
	for (i = 0; i < digits; i++) {
		uint8_t n = (v >> (4 * (digits - 1 - i))) & 0xF;
		buf[i] = n < 10 ? '0' + n : 'A' + n - 10;
	}
	buf[digits] = 0;
	txt_put(x, y, c, buf);
	return digits;
}

uint8_t txt_dec(uint8_t x, uint8_t y, enum txt_col c, uint32_t v)
{
	char buf[11];
	int8_t i = 10;
	buf[i] = 0;
	do {
		buf[--i] = '0' + v % 10;
		v /= 10;
	} while (v);
	txt_put(x, y, c, buf + i);
	return 10 - i;
}
