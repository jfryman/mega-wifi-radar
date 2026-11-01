/* Boot sound (lode/ui/boot-sound.md). PSG tone 0: turbine whine. Tone 1: radio voice. Tone 2: silent, its
 * divider clocks the noise so the roar follows rpm. Noise: roar, or squelch while a burst lasts.
 * FM 1-2: the two engines' drone, 1.6% apart so they beat. FM 3: bass, an octave below.
 * V-Int safe: PSG byte writes to $C00011, FM with the Z80 bus held, 16-bit arithmetic only. */
#include "sfx.h"

#define PSG         (*(vu8 *)0xC00011)
#define NOISE_ROAR  0x07            /* white, clocked by tone 2 */
#define NOISE_HISS  0x04            /* white, fastest fixed rate */

/* FM voices: algorithm 7 (four carriers), sustaining while keyed; loudness = TL base + attenuation */
struct fm_voice { u8 fbalg; u8 dtmul[4]; u8 tl[4]; };
static const struct fm_voice engine = { 4 << 3 | 7, { 0x01, 0x02, 0x03, 0x31 }, { 30, 36, 46, 40 } };
static const struct fm_voice bass   = { 6 << 3 | 7, { 0x01, 0x01, 0x00, 0x02 }, { 28, 26, 22, 52 } };
static const u8 op_off[4] = { 0, 8, 4, 12 };            /* OP1..OP4 register offsets */

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

static void fm_load(u8 ch, const struct fm_voice *v)
{
    YM2612_writeReg(0, 0xB0 + ch, v->fbalg);
    YM2612_writeReg(0, 0xB4 + ch, 0xC0);                /* both speakers */
    for (u8 o = 0; o < 4; o++) {
        u8 r = op_off[o] + ch;
        YM2612_writeReg(0, 0x30 + r, v->dtmul[o]);
        YM2612_writeReg(0, 0x40 + r, 127);              /* silent until the first tick */
        YM2612_writeReg(0, 0x50 + r, 0x1F);             /* instant attack */
        YM2612_writeReg(0, 0x60 + r, 0x00);             /* no decay: sustain while keyed */
        YM2612_writeReg(0, 0x70 + r, 0x00);
        YM2612_writeReg(0, 0x80 + r, 0x0F);             /* fast release */
        YM2612_writeReg(0, 0x90 + r, 0x00);
    }
}

static void fm_freq(u8 ch, u8 block, u16 fn)
{
    YM2612_writeReg(0, 0xA4 + ch, block << 3 | fn >> 8);   /* the high byte first: it latches */
    YM2612_writeReg(0, 0xA0 + ch, fn & 0xFF);
}

static void fm_level(u8 ch, const struct fm_voice *v, u16 att)
{
    for (u8 o = 0; o < 4; o++) {
        u16 tl = v->tl[o] + att;
        YM2612_writeReg(0, 0x40 + op_off[o] + ch, tl > 127 ? 127 : tl);
    }
}

void sfx_on(bool o)
{
    for (u8 ch = 0; ch < 4; ch++) vol(ch, 15);
    bool bus = Z80_getAndRequestBus(TRUE);
    for (u8 ch = 0; ch < 3; ch++) YM2612_writeReg(0, 0x28, ch);          /* key off FM 1-3 */
    if (o) {
        YM2612_writeReg(0, 0x22, 0x00);                 /* no LFO */
        YM2612_writeReg(0, 0x27, 0x00);                 /* channel 3 normal */
        fm_load(0, &engine);
        fm_load(1, &engine);
        fm_load(2, &bass);
        for (u8 ch = 0; ch < 3; ch++) YM2612_writeReg(0, 0x28, 0xF0 | ch);   /* key on: TL does the rest */
    }
    if (!bus) Z80_releaseBus();
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

    /* FM: twin engines + bass, all following rpm; 3 TL steps per PSG volume step */
    u16 fn = 250 + ((u16)pitch_rpm << 1);
    u16 level = 15 - (rpm >> 4), q3 = (quiet << 1) + quiet;
    u16 att = rpm ? (level << 1) + level + q3 : 127;
    u16 att_bass = rpm ? (level << 1) + q3 : 127;      /* the bass carries a little more */
    bool bus = Z80_getAndRequestBus(TRUE);
    fm_freq(0, 3, fn);
    fm_freq(1, 3, fn + (fn >> 6));
    fm_freq(2, 2, fn);
    fm_level(0, &engine, att);
    fm_level(1, &engine, att);
    fm_level(2, &bass, att_bass);
    if (!bus) Z80_releaseBus();
}
