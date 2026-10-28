#include "settings.h"
#include "airports.h"
#include "devcfg.h"
#include "theme.h"

const u16 range_steps[RANGE_COUNT] = { 5, 10, 25, 50, 100 };

struct settings settings;

/* Flash record in ESP32 user_data sector 0 (mw_flash_*: 4 KiB sectors). */
#define SAVE_SECTOR     0
#define SAVE_MAGIC      "MWR1"

struct saved {
    char magic[4];
    struct settings s;
    u16 sum;
};

static inline u16 checksum(const struct saved *v)
{
    const u8 *p = (const u8 *)v;
    u16 sum = 0x5A5A;

    for (u16 i = 0; i < sizeof(*v) - sizeof(v->sum); i++) sum = (sum << 1 | sum >> 15) ^ p[i];
    return sum;
}

s16 airport_find(const char *icao)
{
    for (u16 i = 0; i < AIRPORT_COUNT; i++)
        if (airports[i].icao[0] == icao[0] && airports[i].icao[1] == icao[1] &&
                airports[i].icao[2] == icao[2] && airports[i].icao[3] == icao[3])
            return i;
    return -1;
}

void settings_init(void)
{
    u16 best = 0;

    memset(&settings, 0, sizeof(settings));
    for (u16 i = 1; i < RANGE_COUNT; i++) {
        s16 d = (s16)range_steps[i] - (s16)devcfg_range_nm;
        s16 b = (s16)range_steps[best] - (s16)devcfg_range_nm;
        if ((d < 0 ? -d : d) < (b < 0 ? -b : b)) best = i;
    }
    settings.theme = THEME_CLASSIC;
    settings.range_idx = best;
    settings.tag_mode = TAGS_ALL;
    for (u16 i = 0; i < 4 && devcfg_home_icao[i]; i++) settings.icao[i] = devcfg_home_icao[i];
    settings.lat_e4 = devcfg_home_lat_e4;
    settings.lon_e4 = devcfg_home_lon_e4;
    if (devcfg_relay_url)
        for (u16 i = 0; i < RELAY_LEN && devcfg_relay_url[i]; i++) settings.relay[i] = devcfg_relay_url[i];
}

#ifndef FAKE_FEED
#include "ext/mw/megawifi.h"

bool settings_load(void)
{
    struct saved v;
    u8 *p = mw_flash_read(SAVE_SECTOR * 4096, sizeof(v));

    if (!p) return FALSE;
    memcpy(&v, p, sizeof(v));
    if (memcmp(v.magic, SAVE_MAGIC, 4) || v.sum != checksum(&v) ||
            v.s.theme >= THEME_COUNT || v.s.range_idx >= RANGE_COUNT || v.s.tag_mode >= TAGS_MODES)
        return FALSE;
    v.s.icao[4] = 0;
    v.s.relay[RELAY_LEN] = 0;
    settings = v.s;
    return TRUE;
}

bool settings_save(void)
{
    struct saved v;

    memcpy(v.magic, SAVE_MAGIC, 4);
    v.s = settings;
    v.sum = checksum(&v);
    return mw_flash_sector_erase(SAVE_SECTOR) == MW_ERR_NONE &&
           mw_flash_write(SAVE_SECTOR * 4096, (u8 *)&v, sizeof(v)) == MW_ERR_NONE;
}
#else
bool settings_load(void) { return FALSE; }
bool settings_save(void) { return FALSE; }
#endif
