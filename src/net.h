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
    NET_ERR_UDP,       /* couldn't open/send on the discovery socket */
    NET_ERR_NOT_FOUND, /* no relay answered the mDNS query */
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
    bool chunked;      /* no Content-Length: body ended by a zero-length frame */
    u32 content_len;   /* meaningful only when !chunked */
    u32 received;
    u16 chunks;
    u16 frames;        /* VBlanks spent from mw_http_open to last byte */
};

/* mw_init + the user task that runs mw_process(). Call once, first. */
void net_init(void);
/* mw_detect: up to 5 VERSION requests, 2 s each. */
enum net_err net_detect(struct net_module_info *info);
/* If ssid != NULL and slot 0 doesn't already hold it, store ssid/pass with DHCP
 * in slot 0, make it the default and save. Writes ESP flash only on change. */
enum net_err net_ensure_ap(const char *ssid, const char *pass);
/* Join the default AP slot and wait for an IP. ip_out gets 4 bytes. */
enum net_err net_join(u8 ip_out[4]);
#define NET_SSID_LEN 32
struct net_ap {
    char ssid[NET_SSID_LEN + 1];
    s8 rssi;
    u8 open;                    /* no password needed */
};
/* Scan for APs (blocking, a few seconds). Returns the number stored, or -1. */
s16 net_scan(struct net_ap *aps, u16 max);
/* Store ssid/pass in slot 0 (DHCP, default, saved), rejoin, wait for an IP. */
enum net_err net_connect(const char *ssid, const char *pass, u8 ip_out[4]);
/* Write ssid/pass (DHCP) to slot 0 as the default and save, then read it back. net_connect uses it.
 * On failure net_save_step says which step (for the setup screen). */
enum net_save_step { SAVE_OK, SAVE_AP_CFG, SAVE_IP_CFG, SAVE_DEFAULT, SAVE_FLASH, SAVE_VERIFY };
extern enum net_save_step net_save_step;
enum net_err net_save_ap(const char *ssid, const char *pass);
/* Scanning leaves the AP (the firmware scans only while idle). TRUE until something rejoins. */
bool net_rejoin_needed(void);
/* SSID of the default slot into out (NET_SSID_LEN + 1), "" if none. */
void net_current_ssid(char *out);

enum net_err net_http_get(const char *url, net_chunk_cb cb, void *ctx,
                          struct net_http_result *res);

/* Find the relay over mDNS (lode/net/discovery.md): one legacy-unicast PTR query for
 * _mwradar._tcp.local from UDP port 5354, up to 3 tries of ~1 s. On success writes
 * "http://a.b.c.d:port" into url (>= 24 bytes). Needs a joined network. */
enum net_err net_discover(char *url);

#endif
