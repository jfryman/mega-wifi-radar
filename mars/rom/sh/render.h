/* Master SH-2 views (lode/mars/renderer.md): each sets its palette on entry and draws one frame per call. */
#ifndef RENDER_H
#define RENDER_H
#include "genesis.h"

/* text colours: every view keeps PAL0..PAL3 here, so shared text code can use them */
#define C_TXT(p)    (240 + (p))
#define C_SHADOW    244
#define C_DIM       245

void text_palette(const u16 rgb[4]);

void rtext_enter(void);
void rtext_frame(void);
void rsplash_enter(void);
void rsplash_frame(void);
void rradar_enter(void);
void rradar_frame(void);
void rvox_enter(void);
void rvox_frame(void);

#endif
