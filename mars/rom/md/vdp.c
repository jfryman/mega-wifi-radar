/* 68000 side: the Mega Drive VDP shows only a transparent backdrop (the 32X layer is the picture), time is
 * its vblank flag polled, and pad 1 is read once a frame into COMM14 for the SH-2s (lode/mars/link.md). */
#include "genesis.h"
#include "../link.h"

#define VDP_DATA    (*(volatile u16 *)0xC00000)
#define VDP_CTRL    (*(volatile u16 *)0xC00004)
#define VDP_CTRL_L  (*(volatile u32 *)0xC00004)
#define PAD_DATA    (*(volatile u8 *)0xA10003)
#define PAD_CTRL    (*(volatile u8 *)0xA10009)
#define COMM(n)     (*(volatile u16 *)(0xA15120 + (n)))

static u32 frames;
static u8 in_vb;
static u16 pad;
u16 md_stage;                           /* MARS_DEBUG: shown in COMM14's top nibble */

void vdp_init(void)
{
    static const u8 regs[] = {
        0x04, 0x44, 0x30, 0x3C, 0x07, 0x6C, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x81, 0x37, 0x00, 0x02,
        0x01, 0x00, 0x00,
    };
    (void)VDP_CTRL;
    for (u16 i = 0; i < sizeof(regs); i++) VDP_CTRL = 0x8000 | (i << 8) | regs[i];
    VDP_CTRL_L = 0x40000000;            /* VRAM: all zero, so every plane is transparent */
    for (u16 i = 0; i < 0x8000; i++) VDP_DATA = 0;
    VDP_CTRL_L = 0xC0000000;            /* CRAM: black backdrop (index 0) */
    for (u16 i = 0; i < 64; i++) VDP_DATA = 0;
    VDP_CTRL_L = 0x40000010;
    for (u16 i = 0; i < 40; i++) VDP_DATA = 0;
    PAD_CTRL = 0x40;
    PAD_DATA = 0x40;
}

/* 3- and 6-button pads, as SGDK's BUTTON_* bits. */
static u16 read_pad(void)
{
    u8 a, b, c;
    u16 v;

    PAD_DATA = 0x40; __asm__ volatile("nop\n nop");
    a = PAD_DATA;                       /* ?1CBRLDU */
    PAD_DATA = 0x00; __asm__ volatile("nop\n nop");
    b = PAD_DATA;                       /* ?0SA00DU */
    v = (~a & 0x3F) | ((~b & 0x30) << 2);
    PAD_DATA = 0x40; __asm__ volatile("nop\n nop");
    PAD_DATA = 0x00; __asm__ volatile("nop\n nop");
    PAD_DATA = 0x40; __asm__ volatile("nop\n nop");
    PAD_DATA = 0x00; __asm__ volatile("nop\n nop");
    c = PAD_DATA;
    if (!(c & 0x0F)) {                  /* 6-button: next TH high reads MXYZ */
        PAD_DATA = 0x40; __asm__ volatile("nop\n nop");
        c = PAD_DATA;
        v |= (~c & 0x0F) << 8;
        PAD_DATA = 0x00; __asm__ volatile("nop\n nop");
    }
    PAD_DATA = 0x40;
    return v;
}

u32 md_tick(void)
{
    u8 vb = (VDP_CTRL & 8) != 0;
    if (vb && !in_vb) {
        frames++;
        pad = read_pad();
        COMM(LK_PAD) = pad | md_stage << 12;
    }
    in_vb = vb;
    return frames;
}

u16 md_pad(void) { return pad; }

void md_wait_frames(u16 n)
{
    u32 t = md_tick();
    while (md_tick() - t < n) md_pump();
}
