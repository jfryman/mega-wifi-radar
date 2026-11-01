/* Boot sound on the SN76489 PSG (lode/ui/boot-sound.md). Tone 0: turbine whine. Tone 1: radio voice.
 * Tone 2: silent, its divider clocks the noise so the roar follows rpm. Noise: roar, or squelch while a
 * burst lasts. V-Int safe: byte writes to $C00011, 16-bit arithmetic only. */
#include "sfx.h"

#define PSG         (*(vu8 *)0xC00011)
#define NOISE_ROAR  0x07            /* white, clocked by tone 2 */
#define NOISE_HISS  0x04            /* white, fastest fixed rate */

static bool on;
static u8 squelch_left, squelch_vol, voice_vol, letters, noise_mode;
static u16 seed = 0xACE1;

static void tone(u8 ch, u16 n)
{
    PSG = 0x80 | ch << 5 | (n & 15);
    PSG = (n >> 4) & 0x3F;
}

static void vol(u8 ch, u8 v)
{
    PSG = 0x90 | ch << 5 | (v > 15 ? 15 : v);
}

static void noise(u8 mode)          /* only on change: a write resets the LFSR, which clicks */
{
    if (mode == noise_mode) return;
    noise_mode = mode;
    PSG = 0xE0 | mode;
}

static u16 rnd(void)
{
    seed = (seed >> 1) ^ (-(seed & 1) & 0xB400);
    return seed;
}

void sfx_on(bool o)
{
    for (u8 ch = 0; ch < 4; ch++) vol(ch, 15);
    noise_mode = 0xFF;
    squelch_left = letters = 0;
    voice_vol = 15;
    on = o;
}

void sfx_squelch(u8 frames, u8 v)
{
    squelch_left = frames;
    squelch_vol = v;
}

void sfx_letter(bool tower, char c)
{
    if (!on) return;
    if (c <= ' ') { voice_vol = 15; letters = 0; return; }   /* a gap between words */
    if (letters++ % 3) return;
    tone(1, (tower ? 90 : 150) + (rnd() & 63));
    voice_vol = 8;
}

void sfx_tick(u8 rpm, u8 pitch_rpm, u8 duck, u8 away)
{
    if (!on) return;
    u8 quiet = duck + away;

    tone(0, 240 - ((u16)pitch_rpm * 11 >> 4));
    vol(0, 15 - (rpm >> 5) + quiet);
    tone(2, 700 - ((u16)pitch_rpm << 1));
    vol(2, 15);
    if (squelch_left) {
        squelch_left--;
        noise(NOISE_HISS);
        vol(3, squelch_vol);
    } else {
        noise(NOISE_ROAR);
        vol(3, 15 - ((u16)rpm * 10 >> 8) + quiet);
    }
    vol(1, voice_vol);
    if (voice_vol < 15) voice_vol = voice_vol > 13 ? 15 : voice_vol + 2;
}
