#include <genesis.h>

/* MegaWiFi Rev.C: flat ROM, no SRAM. SGDK's default header declares SRAM at
 * $200000, which on this cart is ROM, so the SRAM fields are blanked here. */
__attribute__((externally_visible))
const ROMHeader rom_header = {
    "SEGA MEGAWIFI   ",
    "(C)JFRY 2026    ",
    "MEGA WIFI RADAR                                 ",
    "MEGA WIFI RADAR                                 ",
    "GM 00000000-00",
    0x000,
    "J               ",
    0x00000000,
    0x003FFFFF,
    0xE0FF0000,
    0xE0FFFFFF,
    "  ",
    0x2020,
    0x20202020,
    0x20202020,
    "            ",
    "ATC radar scope over MegaWiFi           ",
    "JUE             "
};
