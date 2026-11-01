/* The slice of SGDK's MegaWiFi API that src/settings.c uses, as RPCs to the 68000 (sh/mw32.c). */
#ifndef MARS_MEGAWIFI_H
#define MARS_MEGAWIFI_H
#include "genesis.h"
enum mw_err { MW_ERR_NONE = 0, MW_ERR };
u8 *mw_flash_read(u32 addr, u16 data_len);
enum mw_err mw_flash_sector_erase(u16 sect);
enum mw_err mw_flash_write(u32 addr, u8 *data, u16 data_len);
enum mw_err mw_ap_disassoc(void);
enum mw_err mw_factory_settings(void);
#endif
