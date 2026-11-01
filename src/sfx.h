#ifndef SFX_H
#define SFX_H

/* Boot sound on the PSG (lode/ui/boot-sound.md): engine whine + roar, radio squelch and a garbled
 * voice. Driven from the splash V-Int; the 68000 writes the PSG directly, no Z80 bus needed. */
#include <genesis.h>

/* Silence all four voices; on = the engines may start (from rpm 0). */
void sfx_on(bool on);
/* A noise burst over the roar for `frames` frames at attenuation `vol` (0 loud .. 15 off). */
void sfx_squelch(u8 frames, u8 vol);
/* A letter was typed: every few letters a new voice syllable, spaces cut it. */
void sfx_letter(bool tower, char c);
/* Once per frame: rpm 0..255 sets the volume, pitch_rpm the pitch; duck/away attenuate the engines. */
void sfx_tick(u8 rpm, u8 pitch_rpm, u8 duck, u8 away);

#endif
