#include "shared.h"
#include "mars.h"

volatile struct shared sh SHARED;
volatile u32 mars_vtimer SHARED;        /* [m] the master's V-Int */
static volatile u8 lock_want[2] SHARED;
static volatile u8 lock_turn SHARED;

void lock_take(u8 cpu)
{
    u8 other = cpu ^ 1;
    lock_want[cpu] = 1;
    lock_turn = other;
    while (lock_want[other] && lock_turn == other) {}
}

void lock_give(u8 cpu) { lock_want[cpu] = 0; }

u16 mars_pad(void) { return MARS_COMM(14); }

void mars_wait_frame(void)
{
    u32 t = mars_vtimer;
    while (mars_vtimer == t) {}
}

void view_set(enum view v)
{
    sh.view = v;
    u32 seq = ++sh.view_seq;
    while (sh.view_ack != seq) {}
}
