! 32X spike, SH-2 side: the cartridge image starts here.
!   ROM $000  68000 vectors (all point at $3F0, where the 68000 starts in 32X carts)
!   ROM $100  Mega Drive header ("SEGA 32X")
!   ROM $200  68000 exception jump table into md/boot.s ($880800...)
!   ROM $3C0  MARS header: where the SH-2 boot ROMs copy .data from, and the SH-2 entry points
!   ROM $3F0  the 68000 program (md.bin: Sega's boot code, then ours)
! .data (vector tables + start code) is copied by the 32X boot ROM to SDRAM $06000000.
! Interrupts stay masked on both SH-2s; everything polls. Layout after Chilly Willy's / marsdev's
! mars_start.s (MIT, andwn/marsdev).

	.section .text.header, "ax"

	.long	0x01000000
	.rept	63
	.long	0x000003F0
	.endr

	.org	0x100
	.ascii	"SEGA 32X        "
	.ascii	"(C)JFRY 2026.OCT"
	.ascii	"MEGA WIFI RADAR 32X SPIKE                       "
	.ascii	"MEGA WIFI RADAR 32X SPIKE                       "
	.ascii	"GM 00000000-00"
	.word	0
	.ascii	"J6              "
	.long	0x00000000, 0x003FFFFF
	.long	0x00FF0000, 0x00FFFFFF
	.ascii	"            "
	.ascii	"            "
	.ascii	"                                        "
	.ascii	"JUE             "

! 68000 jmp/jsr (addr).l
.macro	md_jmp	addr
	.word	0x4EF9, \addr >> 16, \addr & 0xFFFF
.endm
.macro	md_jsr	addr
	.word	0x4EB9, \addr >> 16, \addr & 0xFFFF
.endm

	.org	0x200
	md_jmp	0x880800	! reset = hot start
	.rept	10
	md_jsr	0x880806	! bus error .. line F
	.endr
	.space	72		! reserved
	md_jsr	0x880806	! spurious
	md_jsr	0x880806	! level 1
	md_jsr	0x880806	! level 2
	md_jsr	0x880806	! level 3
	md_jmp	0x88080C	! level 4 (HBlank)
	md_jsr	0x880806	! level 5
	md_jmp	0x880812	! level 6 (VBlank)
	md_jsr	0x880806	! level 7
	.rept	16
	md_jsr	0x880806	! TRAP #0 .. #15
	.endr
	.space	166		! reserved

	.org	0x3C0
	.ascii	"MWRADAR 32XSPIKE"	! module name
	.long	0x00000000		! version
	.long	_stext			! .data source (ROM offset)
	.long	0x00000000		! destination (SDRAM offset)
	.long	_sdata			! .data size
	.long	0x06000240		! master SH-2 entry (mstart)
	.long	0x06000244		! slave SH-2 entry (sstart)
	.long	0x06000000		! master VBR
	.long	0x06000120		! slave VBR

	.org	0x3F0
	.incbin	"md.bin"
	.align	2

! ------------------------------------------------------------------ SDRAM ($06000000)

	.section .data.boot, "aw"

	! master vector table, $06000000 (72 entries)
	.long	mstart, 0x0603F000, mstart, 0x0603F000
	.rept	68
	.long	m_err
	.endr
	! slave vector table, $06000120
	.long	sstart, 0x06040000, sstart, 0x06040000
	.rept	68
	.long	s_err
	.endr

	.global	mstart, sstart
mstart:				! $06000240
	bra	mcont
	nop
sstart:				! $06000244
	bra	scont
	nop

mcont:
	mov.l	.Lm_stk, r15
	mov.l	.Lm_cctl, r0
	mov	#0x10, r1	! purge the cache, leave it off while clearing .bss
	mov.b	r1, @r0
	mov.l	.Lm_bss0, r1
	mov.l	.Lm_bss1, r2
	mov	#0, r0
1:	cmp/hs	r2, r1
	bt	2f
	mov.l	r0, @r1
	bra	1b
	add	#4, r1
2:	mov.l	.Lm_comm0, r3	! the 68000 clears COMM0 ("M_OK") once it has set FM
	mov.l	.Lm_ok, r1
3:	mov.l	@r3, r2
	cmp/eq	r1, r2
	bt	3b
	mov	#0, r0
	mov.l	r0, @(4, r3)	! clear COMM4 ("S_OK"): release the slave
	mov.l	.Lm_adapter, r1
	mov.b	r0, @(1, r1)	! no 32X interrupt sources
	mov	#0x0F, r0
	shll2	r0
	shll2	r0
	ldc	r0, sr		! SR.I = 15: interrupts masked
	mov.l	.Lm_cctl, r0
	mov	#0x11, r1	! purge + enable the cache
	mov.b	r1, @r0
	mov.l	.Lm_main, r0
	jsr	@r0
	nop
m_err:
	bra	m_err
	nop

	.align	2
.Lm_stk:	.long	0x0603F000
.Lm_cctl:	.long	0xFFFFFE92
.Lm_bss0:	.long	__bss_start
.Lm_bss1:	.long	__bss_end
.Lm_comm0:	.long	0x20004020
.Lm_ok:		.ascii	"M_OK"
.Lm_adapter:	.long	0x20004000
.Lm_main:	.long	_m_main

scont:
	mov.l	.Ls_stk, r15
	mov.l	.Ls_comm4, r3
	mov.l	.Ls_ok, r1
1:	mov.l	@r3, r2		! wait for the master to clear "S_OK"
	cmp/eq	r1, r2
	bt	1b
	mov	#0x0F, r0
	shll2	r0
	shll2	r0
	ldc	r0, sr
	mov.l	.Ls_cctl, r0
	mov	#0x11, r1
	mov.b	r1, @r0
	mov.l	.Ls_main, r0
	jsr	@r0
	nop
s_err:
	bra	s_err
	nop

	.align	2
.Ls_stk:	.long	0x06040000
.Ls_comm4:	.long	0x20004024
.Ls_ok:		.ascii	"S_OK"
.Ls_cctl:	.long	0xFFFFFE92
.Ls_main:	.long	_s_main
