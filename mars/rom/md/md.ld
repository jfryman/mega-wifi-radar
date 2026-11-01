/* 32X ROM, 68000 side. With ADEN = 1 the cart's first 512 KiB is at $880000; this program
 * sits at ROM offset $3F0 (sh/start.s .incbin's md.bin there), so it is linked at $8803F0.
 * Everything must stay inside the fixed 512 KiB window. */
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY
{
    rom (rx)  : ORIGIN = 0x008803F0, LENGTH = 0x0007FC10
    ram (rwx) : ORIGIN = 0x00FF0000, LENGTH = 0x0000F000
}

SECTIONS
{
    .text : {
        KEEP(*(.text.keepboot))
        *(.text .text.*)
        . = ALIGN(2);
        *(.rodata .rodata.*)
        . = ALIGN(2);
    } > rom

    .data : {
        __data_start = .;
        *(.ramcode)
        *(.data .data.*)
        . = ALIGN(2);
        __data_end = .;
    } > ram AT > rom
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(2);
        __bss_end = .;
    } > ram

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

/* The ROM header's jump table (sh/start.s) hard-codes these entry points. */
ASSERT(_irq_jmptab == 0x00880800, "32X boot code must be exactly 1040 bytes: _irq_jmptab != $880800")
