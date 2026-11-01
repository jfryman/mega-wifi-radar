/* 32X spike, SH-2 side: this link produces the whole cartridge image.
 * .text is the ROM (SH-2 view $02000000, file offset 0): header + md.bin first, then SH-2 code.
 * .data is copied by the 32X boot ROM from ROM offset _stext to SDRAM $06000000 (_sdata bytes). */
OUTPUT_ARCH(sh)
ENTRY(mstart)

MEMORY
{
    rom (rx) : ORIGIN = 0x02000000, LENGTH = 0x00400000
    ram (wx) : ORIGIN = 0x06000000, LENGTH = 0x0003F000
}

SECTIONS
{
    .text 0x02000000 : AT(0) {
        KEEP(*(.text.header))
        *(.text .text.*)
        . = ALIGN(4);
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > rom
    _stext = SIZEOF(.text);

    .data 0x06000000 : AT(SIZEOF(.text)) {
        KEEP(*(.data.boot))
        *(.data .data.*)
        . = ALIGN(4);
    } > ram
    _sdata = SIZEOF(.data);

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ram

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

/* The MARS header ($3C0) hard-codes these. */
ASSERT(mstart == 0x06000240, "mstart must be at $06000240")
ASSERT(sstart == 0x06000244, "sstart must be at $06000244")
