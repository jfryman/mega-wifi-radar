#include "feed.h"

const struct ar_rec *feed_rec(const struct ar_hdr *hdr, u16 i)
{
    return (const struct ar_rec *)((const u8 *)hdr + AR_HDR_SIZE + i * hdr->rec_size);
}

const char *feed_err_str(enum feed_err e)
{
    switch (e) {
    case FEED_OK:         return "OK";
    case FEED_ERR_CONFIG: return "NO RELAY";   /* AUTO found none */
    case FEED_ERR_HTTP:   return "ERR HTTP";
    case FEED_ERR_RECV:   return "ERR RECV";
    case FEED_ERR_SIZE:   return "ERR SIZE";
    case FEED_ERR_FORMAT: return "ERR FRAME";
    }
    return "ERR";
}
