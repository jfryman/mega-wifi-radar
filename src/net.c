#include "net.h"
#include "arframe.h"

/* Command buffer shared with the MegaWiFi driver; also reused for HTTP body
 * chunks, which is safe because body reception is the only call in flight. */
static u16 cmd_buf[MW_BUFLEN / 2];

#define AP_SLOT          0
#define ASSOC_TOUT       MW_MS_TO_FRAMES(30000)
#define HTTP_TOUT        MW_MS_TO_FRAMES(20000)
#define RECV_TOUT        MW_MS_TO_FRAMES(10000)
/* mw-fw-rtos (main/http.c) reports this length for a chunked response, then
 * forwards the body and ends it with a zero-length frame on MW_HTTP_CH. */
#define HTTP_LEN_CHUNKED 0x7FFFFFFFUL

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
 * Idles until mw_init() has returned: lsd/comm state isn't valid before that.
 *
 * The memory barrier is required. With LTO, mw_process() is inlined here and
 * gcc hoists the loads of SGDK's (non-volatile) activeCommType and lsd state
 * out of the infinite loop. This task starts before mw_init(), so it cached a
 * NULL driver forever and never serviced the UART. That only showed on cold
 * boots: a soft reset keeps RAM, so the stale pointer happened to be valid. */
static void user_tsk(void)
{
    while (TRUE) {
        __asm__ volatile ("" ::: "memory");
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

    if (!ssid) return NET_OK;
    /* Stored strings are zero padded (32/64 bytes), so a bounded compare is enough. */
    if (mw_ap_cfg_get(AP_SLOT, &cur_ssid, &cur_pass, &phy) == MW_ERR_NONE &&
            str_eq_n(cur_ssid, ssid, 32) && str_eq_n(cur_pass, pass, 64) &&
            mw_def_ap_cfg_get() == AP_SLOT)
        return NET_OK;

    return net_save_ap(ssid, pass);
}

enum net_save_step net_save_step;

/* Always write and save, then read back what the module holds: the in-memory config can match
 * while the flash copy doesn't (a save that never landed), so never skip the save. */
enum net_err net_save_ap(const char *ssid, const char *pass)
{
    char *cur_ssid, *cur_pass;
    enum mw_phy_type phy;
    struct mw_ip_cfg dhcp;

    memset(&dhcp, 0, sizeof(dhcp));   /* IP 0.0.0.0 => DHCP */
    net_save_step = SAVE_AP_CFG;
    if (mw_ap_cfg_set(AP_SLOT, ssid, pass, MW_PHY_11BGN) != MW_ERR_NONE) return NET_ERR_CFG;
    net_save_step = SAVE_IP_CFG;
    if (mw_ip_cfg_set(AP_SLOT, &dhcp) != MW_ERR_NONE) return NET_ERR_CFG;
    net_save_step = SAVE_DEFAULT;
    if (mw_def_ap_cfg(AP_SLOT) != MW_ERR_NONE) return NET_ERR_CFG;
    net_save_step = SAVE_FLASH;
    if (mw_cfg_save() != MW_ERR_NONE) return NET_ERR_CFG;
    net_save_step = SAVE_VERIFY;
    if (mw_ap_cfg_get(AP_SLOT, &cur_ssid, &cur_pass, &phy) != MW_ERR_NONE ||
            !str_eq_n(cur_ssid, ssid, 32) || mw_def_ap_cfg_get() != AP_SLOT)
        return NET_ERR_CFG;
    net_save_step = SAVE_OK;
    return NET_OK;
}

static bool left_ap;     /* net_scan left the AP; nothing has rejoined since */

bool net_rejoin_needed(void) { return left_ap; }

s16 net_scan(struct net_ap *aps, u16 max)
{
    char *data;
    u8 count;

    /* AP_SCAN is idle-only in the firmware (idleCmdMask): leave the AP first */
    mw_ap_disassoc();
    left_ap = TRUE;
    s16 len = mw_ap_scan(MW_PHY_11BGN, &data, &count);
    struct mw_ap_data ap;
    s16 pos = 0;
    u16 n = 0;

    if (len <= 0) return -1;
    /* data lives in the command buffer: copy out before the next mw_ call */
    while (n < max && (pos = mw_ap_fill_next(data, pos, &ap, len)) > 0) {
        u16 l = ap.ssid_len > NET_SSID_LEN ? NET_SSID_LEN : ap.ssid_len;
        if (!l) continue;                       /* hidden network */
        memcpy(aps[n].ssid, ap.ssid, l);
        aps[n].ssid[l] = 0;
        aps[n].rssi = ap.rssi;
        aps[n].open = ap.auth == MW_SEC_OPEN;
        n++;
    }
    return n;
}

void net_current_ssid(char *out)
{
    char *ssid, *pass;
    enum mw_phy_type phy;
    s16 slot = mw_def_ap_cfg_get();

    out[0] = 0;
    if (slot < 0) slot = AP_SLOT;
    if (mw_ap_cfg_get(slot, &ssid, &pass, &phy) != MW_ERR_NONE) return;
    for (u16 i = 0; i < NET_SSID_LEN && ssid[i]; i++) { out[i] = ssid[i]; out[i + 1] = 0; }
}

enum net_err net_connect(const char *ssid, const char *pass, u8 ip_out[4])
{
    enum net_err e;

    mw_ap_disassoc();
    if ((e = net_save_ap(ssid, pass)) != NET_OK) return e;
    return net_join(ip_out);
}

enum net_err net_join(u8 ip_out[4])
{
    s16 slot = mw_def_ap_cfg_get();
    struct mw_ip_cfg *ip;

    if (slot < 0) slot = AP_SLOT;
    left_ap = FALSE;
    /* Already joined (e.g. setup's Wi-Fi page just connected): AP_JOIN is idle-only in the
     * firmware and would be refused, so use the current IP. IP_CURRENT only works when joined. */
    if (mw_ip_current(&ip) == MW_ERR_NONE && ip->addr.addr) {
        memcpy(ip_out, ip->addr.byte, 4);
        return NET_OK;
    }
    if (mw_ap_assoc(slot) != MW_ERR_NONE ||
            mw_ap_assoc_wait(ASSOC_TOUT) != MW_ERR_NONE)
        return NET_ERR_ASSOC;
    if (mw_ip_current(&ip) != MW_ERR_NONE)
        return NET_ERR_IP;
    memcpy(ip_out, ip->addr.byte, 4);
    return NET_OK;
}

static enum net_err http_get(const char *url, net_chunk_cb cb, void *ctx,
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
    res->chunked = res->content_len == HTTP_LEN_CHUNKED;

    while (res->chunked || res->received < res->content_len) {
        ch = MW_HTTP_CH;
        len = MW_BUFLEN;
        if (mw_recv_sync(&ch, (char *)cmd_buf, &len, RECV_TOUT) != MW_ERR_NONE ||
                ch != MW_HTTP_CH)
            return NET_ERR_RECV;
        if (!len) {
            if (res->chunked) break;     /* end-of-body marker */
            continue;
        }
        res->received += len;
        res->chunks++;
        if (cb) cb((const u8 *)cmd_buf, len, ctx);
    }
    res->frames = vtimer - start;
    return NET_OK;
}

enum net_err net_http_get(const char *url, net_chunk_cb cb, void *ctx,
                          struct net_http_result *res)
{
    enum net_err e = http_get(url, cb, ctx, res);

    /* A request that fails mid-way leaves the firmware's client in a busy
     * state where the next mw_http_url_set() is refused; cleanup frees it and
     * the next request re-creates it. */
    if (e != NET_OK) mw_http_cleanup();
    return e;
}

/* ------------------------------------------------------------ relay discovery (mDNS) */

#define DISC_CH         1               /* LSD socket channel 1 (HTTP uses MW_HTTP_CH) */
#define DISC_TRIES      3
#define DISC_TOUT       MW_MS_TO_FRAMES(1000)
#define DNS_SRV         33
#define DNS_A           1

/* The query (contract: AR_MDNS_* in arframe.h): the 6-byte reuse-mode prefix for 224.0.0.251:5353,
 * a DNS header with one question, AR_MDNS_SERVICE as labels, then PTR / IN (QM). */
static u16 build_query(u8 *q)
{
    static const u8 head[] = {
        224, 0, 0, 251, 0x14, 0xE9,
        AR_MDNS_QUERY_ID >> 8, AR_MDNS_QUERY_ID & 0xFF, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    };
    const char *s = AR_MDNS_SERVICE;
    u16 n = sizeof(head);

    memcpy(q, head, n);
    while (*s) {
        u16 len_at = n++, l = 0;
        while (*s && *s != '.') { q[n++] = *s++; l++; }
        q[len_at] = l;
        if (*s) s++;
    }
    q[n++] = 0;
    q[n++] = 0; q[n++] = 12;                /* PTR */
    q[n++] = 0; q[n++] = 1;                 /* IN, QM */
    return n;
}

static inline u16 be16(const u8 *p) { return p[0] << 8 | p[1]; }

/* Skip a (possibly compressed) name; returns the offset after it, or 0 if malformed. */
static u16 skip_name(const u8 *m, u16 len, u16 off)
{
    while (off < len) {
        u8 l = m[off];
        if (!l) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : 0;
        off += 1 + l;
    }
    return 0;
}

/* Parse a reply: first SRV gives the port, first A the address. */
static bool parse_reply(const u8 *m, u16 len, u8 ip[4], u16 *port)
{
    bool have_ip = FALSE;

    *port = 0;
    if (len < 12 || be16(m) != AR_MDNS_QUERY_ID || !(m[2] & 0x80)) return FALSE;
    u16 nq = be16(m + 4), nrr = be16(m + 6) + be16(m + 8) + be16(m + 10);
    u16 off = 12;
    while (nq--) {
        if (!(off = skip_name(m, len, off)) || (off += 4) > len) return FALSE;
    }
    while (nrr--) {
        if (!(off = skip_name(m, len, off)) || off + 10 > len) return FALSE;
        u16 type = be16(m + off), rdlen = be16(m + off + 8);
        off += 10;
        if (off + rdlen > len) return FALSE;
        if (type == DNS_SRV && rdlen >= 6 && !*port) *port = be16(m + off + 4);
        if (type == DNS_A && rdlen == 4 && !have_ip) { memcpy(ip, m + off, 4); have_ip = TRUE; }
        off += rdlen;
    }
    return *port != 0 && (have_ip || ip[0]);
}

enum net_err net_discover(char *url)
{
    enum net_err e = NET_ERR_NOT_FOUND;
    u8 ip[4], query[64];
    u16 port = 0, qlen = build_query(query);
    char src_port[6];

    sprintf(src_port, "%u", AR_MDNS_SRC_PORT);
    if (mw_udp_set(DISC_CH, NULL, NULL, src_port) != MW_ERR_NONE) return NET_ERR_UDP;
    for (u16 t = 0; t < DISC_TRIES && e != NET_OK; t++) {
        if (mw_send_sync(DISC_CH, (const char *)query, qlen, DISC_TOUT) != MW_ERR_NONE) {
            e = NET_ERR_UDP;
            break;
        }
        for (;;) {                          /* replies until one is ours or the wait times out */
            u8 ch = DISC_CH;
            s16 len = MW_BUFLEN;
            if (mw_recv_sync(&ch, (char *)cmd_buf, &len, DISC_TOUT) != MW_ERR_NONE) break;
            if (ch != DISC_CH || len <= 6) continue;
            const u8 *p = (const u8 *)cmd_buf;
            memcpy(ip, p, 4);               /* sender: used if the reply has no A record */
            if (parse_reply(p + 6, len - 6, ip, &port)) { e = NET_OK; break; }
        }
    }
    mw_close(DISC_CH);
    if (e == NET_OK) sprintf(url, "http://%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], port);
    return e;
}
