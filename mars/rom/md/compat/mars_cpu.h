/* 68000 side of the SGDK compatibility layer (lode/mars/summary.md). No interrupts: time is the VDP's vblank
 * flag, polled (md/vdp.c). Polling also reads the pad into COMM14 and, once the module is up, pumps
 * mw_process(), so blocking network code keeps the SH-2s supplied with input. */
#ifndef MARS_CPU_H
#define MARS_CPU_H

u32 md_tick(void);              /* poll: returns the frame count */
u16 md_pad(void);               /* last pad read */
void md_wait_frames(u16 n);      /* pumps mw_process() while waiting */
void md_pump(void);             /* md/task.c: mw_process() once the module is up */
void md_hard_reset(void) __attribute__((noreturn));

#define vtimer                  (md_tick())
#define JOY_readJoypad(j)       ((void)(j), md_tick(), md_pad())
#define SYS_disableInts()       do { } while (0)
#define SYS_enableInts()        do { } while (0)
#define SYS_doVBlankProcess()   (md_wait_frames(1), TRUE)
#define SYS_hardReset()         md_hard_reset()


/* SGDK's genesis.h pulls in the MegaWiFi API when MODULE_MEGAWIFI is set: src/net.c relies on it. */
#include "task.h"
#include "ext/mw/megawifi.h"

#endif
