// Voxel Space inner loops for cockpit mode, drawing straight into VRAM (lode/ui/cockpit.md; voxel.c).
//
// struct vox_col { s16 top; s16 roll; u32 cmd; }    8 bytes per logical column
//   top: highest screen row drawn so far; roll: bank shear (rows); cmd: VDP write command for row 0
//
// void vox_step (u32 px, u32 py, s32 dx, s32 dy, struct vox_col *cols, const s16 *ytab, u16 n,
//                const u8 *h, const u8 *c);
// void vox_pairs(...same, n = pairs...)    one map sample per two columns (far field); dx/dy are per pair
//
// px: x in cells, 16.16; py: y in cells x 128, 16.16 (so its high word is row * 128). The map wraps at 128.
// h: per cell, the byte offset of its height in ytab; c: per cell, the colour byte (both nibbles).
// ytab: the screen row of each height at this depth (any s16: rows above the screen are clipped).
// The VDP auto-increment must be 4 (column-major tiles: a column's rows are 4 bytes apart), and nothing
// else may write the VDP while these run. A byte write to the data port puts the byte on both halves of
// the bus, so the VDP stores four identical pixels.
//
// Registers: d0 map index, d1 row / scratch, d2 px, d3 py, d4 dx, d5 dy, d6 count, d7 span length;
//            a0 column, a1 pair's row, a2 jump target, a3 h, a4 ytab, a5 c, a6 VDP data port.

// Test and draw the column at a0; d1 = ytab row (high word clear). end: label after the unrolled writes;
// load: 1 to load a2 with it here (vox_pairs has two copies), 0 if the caller preloaded it.
.macro COLUMN end, load
    add.w   2(%a0),%d1                  // + roll: the screen row
    cmp.w   (%a0),%d1
    jge     9f                          // hidden behind nearer terrain
    tst.w   %d1
    jpl     1f
    moveq   #0,%d1                      // clip at the top (the high word stays clear)
1:  move.w  (%a0),%d7
    sub.w   %d1,%d7                     // rows from y down to the old top (0 if the column was full)
    move.w  %d1,(%a0)
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
    jmp     (%a2,%d7.w)
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
    move.l  76(%sp),%a3
    move.l  80(%sp),%a5
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
