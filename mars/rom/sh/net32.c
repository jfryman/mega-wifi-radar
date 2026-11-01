/* src/net.h on the 32X: the calls the app makes (boot, setup's Wi-Fi page), as RPCs to the 68000's src/net.c. */
#include "net.h"
#include "link_sh.h"
#include "../link.h"

enum net_save_step net_save_step;

void net_init(void) { lk_call(LK_NET_INIT, NULL, 0, NULL, 0, NULL); }

enum net_err net_detect(struct net_module_info *info)
{
    static u8 r[48];
    memset(r, 0, sizeof(r));
    u8 e = lk_call(LK_NET_DETECT, NULL, 0, r, sizeof(r) - 1, NULL);
    info->major = r[0];
    info->minor = r[1];
    info->variant = (const char *)r + 2;
    return e;
}

/* "a\0b\0" into buf, returns the length */
static u16 two(u8 *buf, const char *a, const char *b)
{
    u16 n = strlen(a) + 1, m = strlen(b) + 1;
    memcpy(buf, a, n);
    memcpy(buf + n, b, m);
    return n + m;
}

enum net_err net_ensure_ap(const char *ssid, const char *pass)
{
    u8 buf[NET_SSID_LEN + 70];
    return lk_call(LK_NET_ENSURE_AP, buf, two(buf, ssid ? ssid : "", pass ? pass : ""), NULL, 0, NULL);
}

enum net_err net_join(u8 ip_out[4])
{
    return lk_call(LK_NET_JOIN, NULL, 0, ip_out, 4, NULL);
}

s16 net_scan(struct net_ap *aps, u16 max)
{
    static u8 r[2 + 14 * sizeof(struct net_ap)];
    u8 a[2] = { max >> 8, max };
    u16 got = 0;
    lk_call(LK_NET_SCAN, a, 2, r, sizeof(r), &got);
    s16 n = (s16)(r[0] << 8 | r[1]);
    if (n > (s16)max) n = max;
    if (n > 0) memcpy(aps, r + 2, n * sizeof(struct net_ap));
    return n;
}

enum net_err net_connect(const char *ssid, const char *pass, u8 ip_out[4])
{
    u8 buf[NET_SSID_LEN + 70], r[5] = { 0 };
    u8 e = lk_call(LK_NET_CONNECT, buf, two(buf, ssid, pass), r, 5, NULL);
    memcpy(ip_out, r, 4);
    net_save_step = r[4];
    return e;
}

void net_current_ssid(char *out)
{
    out[0] = 0;
    lk_call(LK_NET_SSID, NULL, 0, out, NET_SSID_LEN + 1, NULL);
    out[NET_SSID_LEN] = 0;
}

bool net_rejoin_needed(void) { return lk_call(LK_NET_REJOIN, NULL, 0, NULL, 0, NULL); }
