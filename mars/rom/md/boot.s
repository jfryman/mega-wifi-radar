| 32X ROM, 68000 side: Sega's boot code at $3F0, the exception jump targets at $880800, C runtime start.
| Linked at $8803F0 (md.ld): this code only ever runs with the 32X adapter enabled (ADEN = 1),
| where cart offset 0 appears at $880000. The SH-2 image (sh/start.s) .incbin's the binary at ROM $3F0.
| Interrupts stay masked: all timing polls the VDP's vblank flag (vdp.c, now()).

        .section .text.keepboot, "ax"

| Sega's 68000 security/boot code (1040 bytes). It enables the adapter, waits for the SH-2 boot
| ROMs, and jumps to the hot-start entry below via the jump table in the ROM header ($200).
| Fetched from pinned marsdev by the build image ($MARS_BOOT), not kept in this repo.
        .include "mars_security.inc"

| $880800: targets of the ROM header's jump table (sh/start.s). jmp abs.l, 6 bytes each.
        .globl  _irq_jmptab
_irq_jmptab:
        .word   0x4EF9
        .long   _start                  | $880800 reset / hot start
        .word   0x4EF9
        .long   _fault                  | $880806 any exception
        .word   0x4EF9
        .long   _rte                    | $88080C level 4 (HBlank)
        .word   0x4EF9
        .long   _rte                    | $880812 level 6 (VBlank)

        .globl  _start
_start:
        move.w  #0x2700, %sr            | interrupts off for good
        lea     __bss_start, %a0        | clear .bss
        lea     __bss_end, %a1
1:      cmpa.l  %a1, %a0
        bcc.s   2f
        clr.b   (%a0)+
        bra.s   1b
2:
        lea     __data_load, %a0        | copy .data from ROM to work RAM
        lea     __data_start, %a1
        lea     __data_end, %a2
3:      cmpa.l  %a2, %a1
        bcc.s   4f
        move.b  (%a0)+, (%a1)+
        bra.s   3b
4:
        move.b  #0, 0xA15107            | RV = 0: the SH-2s may read the cart ROM
5:      cmpi.l  #0x4D5F4F4B, 0xA15120   | wait for "M_OK" from the master SH-2 boot ROM
        bne.s   5b
6:      cmpi.l  #0x535F4F4B, 0xA15124   | and "S_OK" from the slave
        bne.s   6b
        ori.w   #0x8000, 0xA15100       | FM = 1: the SH-2s own the 32X VDP
        move.w  #0x5A00, 0xA15128       | COMM8: link ready, sequence 0 (lode/mars/link.md)
        move.w  #0, 0xA1512E            | COMM14: no buttons
        move.l  #0, 0xA15120            | release the master SH-2 (sh/start.s waits on COMM0)
        jsr     main
7:      bra.s   7b

_rte:
        rte

        .globl  _fault
_fault:
        move.w  #0x2700, %sr            | unexpected exception: paint the backdrop red, halt
        move.l  #0xC0000000, 0xC00004
        move.w  #0x000E, 0xC00000
8:      bra.s   8b

        .section .note.GNU-stack, "", %progbits
