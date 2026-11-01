/* SH-2 side of the SGDK compatibility layer (lode/mars/summary.md). vtimer is the master SH-2's vblank count;
 * the pad is what the 68000 puts in COMM14. SYS_disableInts() takes the inter-CPU lock: where the Genesis code
 * shut out its V-Int renderer, the slave shuts out the master renderer. Only the slave (the app) uses these. */
#ifndef MARS_CPU_H
#define MARS_CPU_H

extern volatile u32 mars_vtimer;        /* sh/shared.c, cache-through */
void lock_take(u8 cpu);
void lock_give(u8 cpu);
u16 mars_pad(void);
void mars_wait_frame(void);
void mars_hard_reset(void) __attribute__((noreturn));

#define vtimer                  (mars_vtimer)
#define JOY_readJoypad(j)       ((void)(j), mars_pad())
#define SYS_disableInts()       lock_take(1)
#define SYS_enableInts()        lock_give(1)
#define SYS_doVBlankProcess()   (mars_wait_frame(), TRUE)
#define SYS_hardReset()         mars_hard_reset()

#endif
