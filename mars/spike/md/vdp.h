/* From jfryman/mw-cart bringup-rom; vblank-polled timing for the 32X spike.
 * Minimal VDP text console: 40x28 cells on plane A, 4 palettes = 4 text colours. */
#ifndef VDP_H
#define VDP_H

#include <stdint.h>

enum txt_col { COL_WHITE = 0, COL_GREEN = 1, COL_RED = 2, COL_YELLOW = 3 };

#define TXT_COLS 40
#define TXT_ROWS 28

void vdp_init(void);
int vdp_is_pal(void);
uint32_t vdp_frames(void);
void txt_clear_row(uint8_t y);
void txt_put(uint8_t x, uint8_t y, enum txt_col c, const char *s);
/* Fixed-width number output; return number of cells written. */
uint8_t txt_hex(uint8_t x, uint8_t y, enum txt_col c, uint32_t v, uint8_t digits);
uint8_t txt_dec(uint8_t x, uint8_t y, enum txt_col c, uint32_t v);

#endif
