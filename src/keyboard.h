#ifndef KEYBOARD_H
#define KEYBOARD_H

/* On-screen keyboard. Edits buf (NUL-terminated, at most max chars) in place.
 * D-pad moves, A types / activates, B deletes, START accepts.
 * Returns TRUE if accepted, FALSE if cancelled (buf restored). */
#include <genesis.h>

bool kb_edit(const char *title, char *buf, u16 max);

#endif
