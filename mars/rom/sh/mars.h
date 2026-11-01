/* 32X registers, SH-2 view (cache-through addresses). */
#ifndef MARS_H
#define MARS_H
#include "genesis.h"

#define MREG16(a)       (*(volatile u16 *)(a))
#define MARS_ADAPTER    MREG16(0x20004000)      /* bit 15 FM; low byte: interrupt mask (this CPU) */
#define MARS_INTMASK    (*(volatile u8 *)0x20004001)
#define MARS_VINT_CLR   MREG16(0x20004016)
#define MARS_COMM(n)    MREG16(0x20004020 + (n))
#define MARS_DISPMODE   MREG16(0x20004100)
#define MARS_FILLEN     MREG16(0x20004104)
#define MARS_FILADR     MREG16(0x20004106)
#define MARS_FILDAT     MREG16(0x20004108)
#define MARS_FBCTL      MREG16(0x2000410A)
#define MARS_CRAM       ((volatile u16 *)0x20004200)
#define MARS_FB         ((volatile u16 *)0x24000000)    /* line table, then pixels */
#define MARS_OVERWRITE  ((volatile u8 *)0x24020000)     /* the frame buffer, but 0 bytes aren't written */
#define SH2_CCR         (*(volatile u8 *)0xFFFFFE92)

#define FB_VBLK         0x8000
#define FB_FEN          0x0002                          /* fill in progress */
#define FB_FS           0x0001
#define MODE_256        0x0001
#define RGB(r, g, b)    ((u16)((r) | (g) << 5 | (b) << 10)) /* 5:5:5 */

/* The cache: write-through, not coherent between the SH-2s. */
static inline void cache_purge(void) { SH2_CCR = 0x10; SH2_CCR = 0x01; }

#endif
