/* 32X ROM, SH-2 side: this link produces the whole cartridge image (lode/mars/summary.md).
 *   ROM $000..   .text: the header and md.bin (sh/start.s); nothing an SH-2 ever reads after boot
 *   SDRAM image  .data: all SH-2 code, constants and data, copied to $06000000 by the 32X boot ROM
 *   .bss         zeroed by the master at start
 *   .shared      state both SH-2s use, at the cache-through alias ($2603xxxx): no stale cache lines
 * SDRAM: $06000000-$0602FFFF image + bss, $06030000-$06037FFF shared, $06038000-$0603BFFF slave stack,
 * $0603C000-$0603FFFF master stack. */
OUTPUT_ARCH(sh)
ENTRY(mstart)

MEMORY
{
    rom (rx) : ORIGIN = 0x02000000, LENGTH = 0x00400000
    ram (wx) : ORIGIN = 0x06000000, LENGTH = 0x00030000
    shr (rw) : ORIGIN = 0x26030000, LENGTH = 0x00008000
}

SECTIONS
{
    .text 0x02000000 : AT(0) {
        KEEP(*(.text.header))
        . = ALIGN(4);
    } > rom
    _stext = SIZEOF(.text);

    .data 0x06000000 : AT(SIZEOF(.text)) {
        KEEP(*(.data.boot))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        . = ALIGN(4);
    } > ram
    _sdata = SIZEOF(.data);

    /* before .bss, so these input sections land here and not there */
    .shared (NOLOAD) : {
        __shared_start = .;
        *(.shared .shared.*)
        *(.bss.settings)
        *(.bss.tr_state)
        *(.bss.tr_brg)
        . = ALIGN(4);
        __shared_end = .;
    } > shr

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ram

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

/* src/traffic.h reaches the track table as overlay_ram + OV_TRACKS (sh/radar32.c asserts 22720) */
_overlay_ram = _mars_tracks - 22720;

ASSERT(mstart == 0x06000240, "mstart must be at $06000240")
ASSERT(sstart == 0x06000244, "sstart must be at $06000244")
