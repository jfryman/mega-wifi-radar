#ifndef NET_H
#define NET_H

#include <genesis.h>

/* Blocking MegaWiFi helpers. Use them at start-up or on setup screens only;
 * they block the supervisor task until the module replies or times out. */

enum net_err {
    NET_OK = 0,
    NET_ERR_NO_MODULE,
    NET_ERR_CFG,
    NET_ERR_ASSOC,
    NET_ERR_IP,
    NET_ERR_HTTP,
    NET_ERR_RECV,
};

struct net_module_info {
    u8 major;
    u8 minor;
    const char *variant;
};

/* Called once per received body chunk (at most MW_BUFLEN bytes). */
typedef void (*net_chunk_cb)(const u8 *data, u16 len, void *ctx);

struct net_http_result {
    s16 status;        /* HTTP status, or < 100 if the request didn't complete */
    u32 content_len;
    u32 received;
    u16 chunks;
    u16 frames;        /* VBlanks spent from mw_http_open to last byte */
};

/* mw_init + the user task that runs mw_process(). Call once, first. */
void net_init(void);
/* Reset the ESP32 and the driver state again (for detect retries). */
void net_reset(void);

/* Bring-up diagnostics for the last net_init/net_reset + net_detect. */
struct net_diag {
    u16 init_frames;     /* VBlanks spent in mw_init (reset waits ~62 if they ran) */
    u16 detect_frames;   /* VBlanks spent in mw_detect */
    u8 lsr;              /* 16C550 LSR after detect (0x60 idle; 0x02 OE, 0x08 FE, 0x01 data) */
    u8 msr;              /* 16C550 MSR after detect (0x10 = CTS: ESP32 ready to receive) */
};
extern struct net_diag net_diag;
enum net_err net_detect(struct net_module_info *info);
/* If ssid != NULL and slot 0 doesn't already hold it, store ssid/pass with DHCP
 * in slot 0, make it the default and save. Writes ESP flash only on change. */
enum net_err net_ensure_ap(const char *ssid, const char *pass);
/* Join the default AP slot and wait for an IP. ip_out gets 4 bytes. */
enum net_err net_join(u8 ip_out[4]);
enum net_err net_http_get(const char *url, net_chunk_cb cb, void *ctx,
                          struct net_http_result *res);

#endif
