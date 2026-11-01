/* SGDK config.h, the part the MegaWiFi driver reads: MegaWiFi Rev.C cart only (as sgdk/patches/0002). */
#ifndef MARS_CONFIG_H
#define MARS_CONFIG_H
#define MODULE_MEGAWIFI                 1
#define MEGAWIFI_IMPLEMENTATION_CROSS   0x01
#define MEGAWIFI_IMPLEMENTATION_MW_CART 0x02
#define MEGAWIFI_IMPLEMENTATION_ED      0x04
#define MEGAWIFI_IMPLEMENTATION         MEGAWIFI_IMPLEMENTATION_MW_CART
#define MODULE_MEGAWIFI_ASYNC           0
#define MODULE_SERIAL                   0
#endif
