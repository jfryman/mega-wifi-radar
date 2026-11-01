/* The MegaWiFi flash calls src/settings.c makes, as RPCs (compat/ext/mw/megawifi.h). */
#include "ext/mw/megawifi.h"
#include "link_sh.h"
#include "shared.h"
#include "../link.h"

u8 *mw_flash_read(u32 addr, u16 data_len)
{
    static u8 buf[128];
    u8 a[6] = { addr >> 24, addr >> 16, addr >> 8, addr, data_len >> 8, data_len };
    if (data_len > sizeof(buf)) return NULL;
    return lk_call(LK_FLASH_READ, a, 6, buf, sizeof(buf), NULL) ? NULL : buf;
}

enum mw_err mw_flash_sector_erase(u16 sect)
{
    u8 a[2] = { sect >> 8, sect };
    return lk_call(LK_FLASH_ERASE, a, 2, NULL, 0, NULL) ? MW_ERR : MW_ERR_NONE;
}

enum mw_err mw_flash_write(u32 addr, u8 *data, u16 data_len)
{
    static u8 a[4 + 128];
    if (data_len > 128) return MW_ERR;
    a[0] = addr >> 24; a[1] = addr >> 16; a[2] = addr >> 8; a[3] = addr;
    memcpy(a + 4, data, data_len);
    return lk_call(LK_FLASH_WRITE, a, 4 + data_len, NULL, 0, NULL) ? MW_ERR : MW_ERR_NONE;
}

enum mw_err mw_ap_disassoc(void) { return lk_call(LK_AP_DISASSOC, NULL, 0, NULL, 0, NULL) ? MW_ERR : MW_ERR_NONE; }
enum mw_err mw_factory_settings(void) { return lk_call(LK_FACTORY, NULL, 0, NULL, 0, NULL) ? MW_ERR : MW_ERR_NONE; }

u16 rom_checksum(void) { return sh.rom_checksum; }
