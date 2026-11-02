/* Chime (lode/ui/notices.md): a few FM notes written straight to the YM2612 by the 68000. SGDK's null Z80
 * driver never touches the chip, so no sound driver is needed, but the YM2612 sits in the Z80's address
 * window ($A04000): the 68000 must hold the Z80 bus while it writes, or the writes are lost. chime_play()
 * loads the patch with interrupts off; chime_tick() only keys notes on and off, so it's safe in the V-Int. */
#include "chime.h"
#include "settings.h"

#define CHANNELS    4               /* notes rotate over FM channels 1-4, so they ring over each other */
#define RING_OUT    90              /* frames after the last key-off before the chime counts as done */

/* One operator: DT/MUL, TL, RS/AR, AM/D1R, D2R, D1L/RR (registers 0x30-0x80) */
struct fm_op { u8 dtmul, tl, rsar, d1r, d2r, d1lrr; };
struct fm_patch { u8 fbalg; struct fm_op op[4]; };     /* op[0..3] = OP1..OP4 */
struct note { u8 at, len, semi; };                      /* frames from the start; semitones above C4 */
struct tune { const struct fm_patch *patch; u8 count; struct note notes[8]; };

/* Bell: algorithm 4, two pairs. OP1 (x7) brightens OP2 (x2) and dies away first, as a struck bell's
 * overtones do; OP3 -> OP4 (x1) is the body. */
static const struct fm_patch bell = { 0x04, {
    { 0x07, 30, 0x1F, 8, 4, 0x36 },
    { 0x02, 14, 0x1F, 4, 2, 0x25 },
    { 0x01, 40, 0x1F, 6, 3, 0x26 },
    { 0x01, 16, 0x1F, 3, 1, 0x25 },
} };

/* Steel guitar: algorithm 4 with feedback on OP1 for the twang; quick decay like a plucked string. */
static const struct fm_patch guitar = { 0x2C, {
    { 0x01, 24, 0x1F, 10, 6, 0x47 },
    { 0x01, 14, 0x1F, 6, 4, 0x36 },
    { 0x03, 36, 0x1F, 12, 6, 0x47 },
    { 0x01, 22, 0x1F, 7, 5, 0x36 },
} };

/* Generic: a terminal's rising four-note chime, C5 E5 G5 C6. */
static const struct tune generic = { &bell, 4, {
    { 0, 45, 12 }, { 9, 45, 16 }, { 18, 45, 19 }, { 27, 60, 24 },
} };

/* KBNA, an easter egg for the owner's home: an original little country lick, E4 G4 A4, a grace note from B4
 * up to C5, A4 C5, ending on a held E5. */
static const struct tune nashville = { &guitar, 8, {
    { 0, 10, 4 }, { 8, 10, 7 }, { 16, 12, 9 }, { 26, 4, 11 }, { 30, 12, 12 }, { 42, 8, 9 }, { 50, 10, 12 },
    { 60, 60, 16 },
} };

/* Alert: algorithm 7 with only OP1 sounding, some feedback for an edge: a plain electronic tone. */
static const struct fm_patch beep = { 0x27, {
    { 0x01, 8, 0x1F, 0, 0, 0x0F },
    { 0x01, 127, 0x1F, 0, 0, 0x0F },
    { 0x01, 127, 0x1F, 0, 0, 0x0F },
    { 0x01, 127, 0x1F, 0, 0, 0x0F },
} };

/* Emergency squawk: hi-lo three times, A5 E5, like a caution tone. */
static const struct tune alert = { &beep, 6, {
    { 0, 10, 21 }, { 14, 10, 16 }, { 28, 10, 21 }, { 42, 10, 16 }, { 56, 10, 21 }, { 70, 14, 16 },
} };

/* F-numbers for C..B at block 4 (A4 = 440 Hz -> 1083 with the NTSC 7.67 MHz clock); block = octave. */
static const u16 fnum[12] = { 644, 682, 723, 766, 811, 859, 910, 965, 1022, 1083, 1147, 1215 };
static const u8 op_off[4] = { 0, 8, 4, 12 };            /* OP1..OP4 register offsets */

static const struct tune *tune;
static u32 start;
static u8 next;                     /* next note to key on */
static u8 keyed;                    /* channel bits currently keyed on */
static u8 off_at[CHANNELS];         /* frame each channel keys off */
static u16 done_at;

static void reg(u16 ch, u8 r, u8 v) { YM2612_writeReg(ch >> 2, r + (ch & 3), v); }  /* ch 0-2, 4-6 */
static u16 chan(u16 i) { return i < 3 ? i : i + 1; }   /* channel i -> YM channel code */

static void play(const struct tune *t);

static void key(u16 i, bool on)
{
    YM2612_writeReg(0, 0x28, (on ? 0xF0 : 0x00) | chan(i));
    if (on) keyed |= 1 << i;
    else keyed &= ~(1 << i);
}

void chime_play(void) { play(memcmp(settings.icao, "KBNA", 4) ? &generic : &nashville); }
void chime_alert(void) { play(&alert); }

static void play(const struct tune *t)
{
    if (settings.sound == SOUND_OFF) return;
    const struct fm_patch *p = t->patch;

    SYS_disableInts();
    bool bus = Z80_getAndRequestBus(TRUE);
    tune = NULL;
    YM2612_writeReg(0, 0x22, 0x00);                     /* no LFO */
    YM2612_writeReg(0, 0x27, 0x00);                     /* channel 3 normal, timers off */
    YM2612_writeReg(0, 0x2B, 0x00);                     /* channel 6 as FM, not DAC */
    for (u16 i = 0; i < CHANNELS; i++) {
        u16 c = chan(i);
        key(i, FALSE);
        reg(c, 0xB0, p->fbalg);
        reg(c, 0xB4, 0xC0);                             /* both speakers */
        for (u16 o = 0; o < 4; o++) {
            const struct fm_op *op = &p->op[o];
            reg(c, 0x30 + op_off[o], op->dtmul);
            reg(c, 0x40 + op_off[o], op->tl);
            reg(c, 0x50 + op_off[o], op->rsar);
            reg(c, 0x60 + op_off[o], op->d1r);
            reg(c, 0x70 + op_off[o], op->d2r);
            reg(c, 0x80 + op_off[o], op->d1lrr);
            reg(c, 0x90 + op_off[o], 0x00);
        }
    }
    const struct note *last = &t->notes[t->count - 1];
    done_at = last->at + last->len + RING_OUT;
    next = 0;
    start = vtimer;
    tune = t;
    if (!bus) Z80_releaseBus();
    SYS_enableInts();
}

void chime_tick(void)
{
    if (!tune) return;
    u32 t = vtimer - start;
    bool due = next < tune->count && tune->notes[next].at <= t;
    for (u16 i = 0; i < CHANNELS; i++) due |= (keyed & (1 << i)) && t >= off_at[i];
    if (!due) {
        if (next == tune->count && !keyed && t >= done_at) tune = NULL;
        return;
    }
    bool bus = Z80_getAndRequestBus(TRUE);
    for (u16 i = 0; i < CHANNELS; i++)
        if ((keyed & (1 << i)) && t >= off_at[i]) key(i, FALSE);
    while (next < tune->count && tune->notes[next].at <= t) {
        const struct note *n = &tune->notes[next];
        u16 i = next % CHANNELS, c = chan(i);
        u16 oct = 4 + n->semi / 12;
        key(i, FALSE);
        reg(c, 0xA4, (oct << 3) | (fnum[n->semi % 12] >> 8));     /* the high byte first: it latches */
        reg(c, 0xA0, fnum[n->semi % 12] & 0xFF);
        key(i, TRUE);
        off_at[i] = n->at + n->len;
        next++;
    }
    if (!bus) Z80_releaseBus();
}
