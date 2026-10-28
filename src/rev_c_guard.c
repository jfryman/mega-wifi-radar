/* Build-time proof that we compile against an SGDK patched for MegaWiFi Rev.C.
 * The headers must agree with the libmd.a they were built with; the docker
 * image builds both from the same patched tree (see sgdk/patches/). */
#include <genesis.h>
#include "ext/mw/16c550.h"

#if !MODULE_MEGAWIFI
#error "SGDK built without MODULE_MEGAWIFI"
#endif
#if MEGAWIFI_IMPLEMENTATION != MEGAWIFI_IMPLEMENTATION_MW_CART
#error "SGDK MEGAWIFI_IMPLEMENTATION must be MEGAWIFI_IMPLEMENTATION_MW_CART"
#endif

_Static_assert(UART_CLK == 20000000LU, "Rev.C UART clock is 20 MHz");
_Static_assert(UART_BR == 1250000LU, "Rev.C UART runs at 1.25 Mbaud");
_Static_assert(UART_CLK / (16 * UART_BR) == 1, "Rev.C UART divisor must be 1");
