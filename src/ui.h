#ifndef UI_H
#define UI_H

/* Text-mode UI helpers for the setup screens. Only used while the radar
 * renderer is stopped (SGDK's normal VBlank processing is in charge). */
#include <genesis.h>

/* Wait a frame and return newly pressed buttons, with auto-repeat on the d-pad. */
u16 ui_pad(void);
/* Wait until no button is held (so the press that opened a screen doesn't act on it). */
void ui_wait_release(void);
/* Clear the screen to the text layout (planes, window, sprites, scroll). */
void ui_clear(void);
/* Draw text at (x, y) in palette pal (PAL0 white .. PAL3 red, see theme.h). */
void ui_text(u16 pal, u16 x, u16 y, const char *s);
/* Draw text padded with spaces to width w (erases what was there). */
void ui_field(u16 pal, u16 x, u16 y, u16 w, const char *s);
/* Show a message on the status line. */
void ui_status(u16 pal, const char *s);

#endif
