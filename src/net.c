#include "net.h"

/* Command buffer shared with the MegaWiFi driver; also reused for HTTP body
 * chunks, which is safe because body reception is the only call in flight. */
static u16 cmd_buf[MW_BUFLEN / 2];

#define AP_SLOT          0
#define ASSOC_TOUT       MW_MS_TO_FRAMES(30000)
#define HTTP_TOUT        MW_MS_TO_FRAMES(20000)
#define RECV_TOUT        MW_MS_TO_FRAMES(10000)

/* SGDK has no strncmp. True if a and b match up to n chars or a shared NUL. */
static bool str_eq_n(const char *a, const char *b, u16 n)
{
    while (n--) {
        if (*a != *b) return FALSE;
        if (!*a) return TRUE;
        a++;
        b++;
    }
    return TRUE;
}

static volatile bool mw_up;

/* Runs in SGDK's user task during VBlank waits. Never touch the VDP here.
 * Idles until mw_init() has returned: lsd/comm state isn't valid before that. */
static void user_tsk(void)
{
    while (TRUE) {
        if (mw_up) mw_process();
    }
}

void net_init(void)
{
    /* The user task must exist BEFORE mw_init(): uart_start() times the ESP32
     * reset with TSK_superPend(), which returns at once when no user task is
     * set. Without the 1 s wait, RTS (-> ESP32 GPIO9 strap) goes active while
     * the chip is still booting, so it boots into download mode and never answers. */
    TSK_userSet(user_tsk);
    mw_init(cmd_buf, MW_BUFLEN);
    mw_up = TRUE;
}

void net_reset(void)
{
    /* mw_init() again = full ESP32 reset + fresh lsd/comm state, which also
     * drops a 68k receiver wedged mid-"frame" by boot-time line garbage. */
    mw_up = FALSE;
    mw_init(cmd_buf, MW_BUFLEN);
    mw_up = TRUE;
}

enum net_err net_detect(struct net_module_info *info)
{
    char *variant = NULL;

    if (mw_detect(&info->major, &info->minor, &variant) != MW_ERR_NONE)
        return NET_ERR_NO_MODULE;
    info->variant = variant ? variant : "?";
    return NET_OK;
}

enum net_err net_ensure_ap(const char *ssid, const char *pass)
{
    char *cur_ssid, *cur_pass;
    enum mw_phy_type phy;
    struct mw_ip_cfg dhcp;

    if (!ssid) return NET_OK;
    /* Stored strings are zero padded (32/64 bytes), so a bounded compare is enough. */
    if (mw_ap_cfg_get(AP_SLOT, &cur_ssid, &cur_pass, &phy) == MW_ERR_NONE &&
            str_eq_n(cur_ssid, ssid, 32) && str_eq_n(cur_pass, pass, 64) &&
            mw_def_ap_cfg_get() == AP_SLOT)
        return NET_OK;

    memset(&dhcp, 0, sizeof(dhcp));   /* IP 0.0.0.0 => DHCP */
    if (mw_ap_cfg_set(AP_SLOT, ssid, pass, MW_PHY_11BGN) != MW_ERR_NONE ||
            mw_ip_cfg_set(AP_SLOT, &dhcp) != MW_ERR_NONE ||
            mw_def_ap_cfg(AP_SLOT) != MW_ERR_NONE ||
            mw_cfg_save() != MW_ERR_NONE)
        return NET_ERR_CFG;
    return NET_OK;
}

enum net_err net_join(u8 ip_out[4])
{
    s16 slot = mw_def_ap_cfg_get();
    struct mw_ip_cfg *ip;

    if (slot < 0) slot = AP_SLOT;
    if (mw_ap_assoc(slot) != MW_ERR_NONE ||
            mw_ap_assoc_wait(ASSOC_TOUT) != MW_ERR_NONE)
        return NET_ERR_ASSOC;
    if (mw_ip_current(&ip) != MW_ERR_NONE)
        return NET_ERR_IP;
    memcpy(ip_out, ip->addr.byte, 4);
    return NET_OK;
}

enum net_err net_http_get(const char *url, net_chunk_cb cb, void *ctx,
                          struct net_http_result *res)
{
    u32 start;
    u8 ch;
    s16 len;

    memset(res, 0, sizeof(*res));
    start = vtimer;
    if (mw_http_url_set(url) != MW_ERR_NONE ||
            mw_http_method_set(MW_HTTP_METHOD_GET) != MW_ERR_NONE ||
            mw_http_open(0) != MW_ERR_NONE)
        return NET_ERR_HTTP;

    res->status = mw_http_finish(&res->content_len, HTTP_TOUT);
    if (res->status < 100)
        return NET_ERR_HTTP;

    while (res->received < res->content_len) {
        ch = MW_HTTP_CH;
        len = MW_BUFLEN;
        if (mw_recv_sync(&ch, (char *)cmd_buf, &len, RECV_TOUT) != MW_ERR_NONE ||
                ch != MW_HTTP_CH)
            return NET_ERR_RECV;
        res->received += len;
        res->chunks++;
        if (cb) cb((const u8 *)cmd_buf, len, ctx);
    }
    res->frames = vtimer - start;
    return NET_OK;
}
