// Voxel Space inner loops, drawing straight into VRAM (lode/plans/cockpit-mode.md).
//
// struct col { s16 top; s16 roll; u32 cmd; }    8 bytes per logical column
//   top:  highest row drawn so far minus roll ("unrolled": compared directly against ytab)
//   roll: bank shear;  cmd: VDP write command for row 0 of the column
//
// void vox_step (u32 px, u32 py, s32 dx, s32 dy, struct col *cols, const s16 *ytab, u16 n);
// void vox_pairs(u32 px, u32 py, s32 dx, s32 dy, struct col *cols, const s16 *ytab, u16 npairs);
//   vox_pairs takes one map sample per two columns (far field); dx/dy are per pair.
// void vox_ytab (s16 *ytab, s32 v, s32 dv);   32 entries: v >> 16, v -= dv (caller checks no clamping is needed)
//
// px: x in Q16 cells; py: y in Q23 cells (so its high word is row * 128). ytab: screen row per height / 2,
// never less than the largest |roll|, so the real row y' + roll >= 0 and nothing needs clipping.
// The VDP auto-increment must be 4 (column-major tiles: a column's rows are 4 bytes apart).
// Colours are bytes with both nibbles set; a byte write to the VDP data port puts the byte on both halves of
// the bus, so the VDP stores four identical pixels.
//
// Registers: d0 map index, d1 row / scratch, d2 px, d3 py, d4 dx, d5 dy, d6 count, d7 span length;
//            a0 column, a1 pair's row, a2 jump target, a3 terrain_h2, a4 ytab, a5 terrain_c, a6 VDP data port.

// Test and draw the column at a0; d1 = ytab row (high word clear). end: label after the unrolled writes;
// load: 1 to load a2 with it here (vox_pairs has two copies), 0 if the caller preloaded it.
.macro COLUMN end, load
    cmp.w   (%a0),%d1
    jge     9f                          // hidden behind nearer terrain
    move.w  (%a0),%d7
    sub.w   %d1,%d7                     // rows from y down to the old top, >= 1
    move.w  %d1,(%a0)
    add.w   2(%a0),%d1                  // real row (>= 0: ytab is clamped)
    add.w   %d1,%d1
    add.w   %d1,%d1
    swap    %d1                         // row * 4 bytes, in command form
    add.l   4(%a0),%d1
    move.l  %d1,4(%a6)                  // control port: write address
    move.b  (%a5,%d0.w),%d1
    add.w   %d7,%d7                     // 2 bytes per unrolled write: jump back 2n bytes from the end
    neg.w   %d7
.if \load
    lea     \end(%pc),%a2
.endif
    jmp     (%a2,%d7.w)                 // 30 cycles + 8 per row, against 8 + 18 per row for a dbra loop
    .rept   112
    move.b  %d1,(%a6)
    .endr
\end:
9:
.endm

// d0 = map index of (px, py); then step px, py; d1 = ytab row of that cell's height (high word clear)
.macro SAMPLE
    move.l  %d2,%d0
    swap    %d0
    and.w   #127,%d0
    move.l  %d3,%d1
    swap    %d1
    and.w   #0x3F80,%d1
    or.w    %d1,%d0
    add.l   %d4,%d2
    add.l   %d5,%d3
    moveq   #0,%d1
    move.b  (%a3,%d0.w),%d1             // ytab byte offset
    move.w  (%a4,%d1.w),%d1
.endm

.macro ENTER
    movem.l %d2-%d7/%a2-%a6,-(%sp)      // 44 bytes + return address: arguments from 48(sp)
    movem.l 48(%sp),%d2-%d5             // px, py, dx, dy
    move.l  64(%sp),%a0
    move.l  68(%sp),%a4
    move.w  74(%sp),%d6
    lea     terrain_h2,%a3
    lea     terrain_c,%a5
    lea     0xC00000,%a6
.endm

    .globl  vox_step
vox_step:
    ENTER
    lea     .Lsend(%pc),%a2
    subq.w  #1,%d6
    jmi     .Lsdone
.Lsloop:
    SAMPLE
    COLUMN  .Lsend, 0
    addq.l  #8,%a0
    dbra    %d6,.Lsloop
.Lsdone:
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

    .globl  vox_pairs
vox_pairs:
    ENTER
    subq.w  #1,%d6
    jmi     .Lpdone
.Lploop:
    SAMPLE
    movea.w %d1,%a1
    COLUMN  .Lpend1, 1
    addq.l  #8,%a0
    moveq   #0,%d1
    move.w  %a1,%d1
    COLUMN  .Lpend2, 1
    addq.l  #8,%a0
    dbra    %d6,.Lploop
.Lpdone:
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

    .globl  vox_ytab
// v and dv as integer and fraction words, so each entry is store + sub + subx (16 cycles, not 24 with swaps)
vox_ytab:
    movem.l %d2-%d3,-(%sp)              // 8 bytes + return address: arguments from 12(sp)
    move.l  12(%sp),%a0
    move.w  16(%sp),%d0                 // v integer
    move.w  18(%sp),%d1                 // v fraction
    move.w  20(%sp),%d2                 // dv integer
    move.w  22(%sp),%d3                 // dv fraction
    .rept   32
    move.w  %d0,(%a0)+
    sub.w   %d3,%d1
    subx.w  %d2,%d0                     // borrow from the fraction
    .endr
    movem.l (%sp)+,%d2-%d3
    rts
