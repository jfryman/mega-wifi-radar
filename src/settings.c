#include "settings.h"
#include "airports.h"
#include "devcfg.h"
#include "theme.h"
#include "tlist.h"

const u16 range_steps[RANGE_COUNT] = { 5, 10, 25, 50, 100 };

struct settings settings;

/* Flash record in ESP32 user_data sector 0 (mw_flash_*: 4 KiB sectors). */
#define SAVE_SECTOR     0
#define SAVE_MAGIC      "MWR3"
#define SAVE_MAGIC_V2   "MWR2"          /* 0.4.0-0.4.1: no sort or spare bytes; loaded with NEAREST */
#define SAVE_MAGIC_V1   "MWR1"          /* 0.3.x: no sound byte either; loaded with sound on */
#define SAVE_GAP_SECS   30
#define MARK_SECTOR     1               /* dev-defaults-off marker, written by a reset */
#define MARK_MAGIC      "MWRN"
#define ROM_CHECKSUM    rom_checksum()  /* header checksum: differs per build */

/* The header checksum at $18E, written into the ROM by sizebnd after compiling. Read it from the
 * bus: the empty asm hides the fixed address from GCC (no -Warray-bounds, no constant folding). */
static u16 __attribute__((unused)) rom_checksum(void)
{
    const volatile u16 *p = (const volatile u16 *)0x18E;
    __asm__("" : "+a"(p));
    return *p;
}

struct saved {
    char magic[4];
    struct settings s;
    u16 sum;
};

/* The 0.4.0 record: struct settings up to sound. */
struct saved_v2 {
    char magic[4];
    struct {
        u8 theme, range_idx, tag_mode;
        char icao[5];
        s32 lat_e4, lon_e4;
        char relay[RELAY_LEN + 1];
        u8 sound;
    } s;
    u16 sum;
};
_Static_assert(__builtin_offsetof(struct settings, sort) == __builtin_offsetof(struct saved_v2, s.sound) - 4 + 1,
               "struct settings must start with the 0.4.0 fields, unchanged");

/* The 0.3.x record: struct settings up to relay. */
struct saved_v1 {
    char magic[4];
    u8 theme, range_idx, tag_mode;
    char icao[5];
    s32 lat_e4, lon_e4;
    char relay[RELAY_LEN + 1];
    u16 sum;
};
_Static_assert(__builtin_offsetof(struct settings, sound) == __builtin_offsetof(struct saved_v1, sum) - 4,
               "struct settings must start with the 0.3.x fields, unchanged");

/* Over the record's bytes up to its sum (n). */
static inline u16 checksum(const void *v, u16 n)
{
    const u8 *p = v;
    u16 sum = 0x5A5A;

    for (u16 i = 0; i < n; i++) sum = (sum << 1 | sum >> 15) ^ p[i];
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
    settings.sound = SOUND_ON;
}

void settings_dev_defaults(void)
{
    if (devcfg_relay_url)
        for (u16 i = 0; i < RELAY_LEN && devcfg_relay_url[i]; i++) settings.relay[i] = devcfg_relay_url[i];
}

static bool dirty;
static u32 saved_at;

void settings_changed(void) { dirty = TRUE; }

bool settings_save_due(void)
{
    if (!dirty || (saved_at && vtimer - saved_at < SAVE_GAP_SECS * 60UL)) return FALSE;
    dirty = FALSE;                          /* a failed write isn't retried until the next change */
    saved_at = vtimer;
    return TRUE;
}

#ifndef FAKE_FEED
#include "ext/mw/megawifi.h"

bool settings_load(void)
{
    struct saved v;
    const struct saved_v2 *o2 = (const struct saved_v2 *)&v;
    const struct saved_v1 *o1 = (const struct saved_v1 *)&v;
    struct settings s;
    u8 *p = mw_flash_read(SAVE_SECTOR * 4096, sizeof(v));

    if (!p) return FALSE;
    memcpy(&v, p, sizeof(v));
    memset(&s, 0, sizeof(s));               /* fields an older record lacks: 0 = their default */
    if (!memcmp(v.magic, SAVE_MAGIC, 4) && v.sum == checksum(&v, sizeof(v) - sizeof(v.sum))) {
        s = v.s;
    } else if (!memcmp(v.magic, SAVE_MAGIC_V2, 4) && o2->sum == checksum(o2, sizeof(*o2) - sizeof(o2->sum))) {
        memcpy(&s, &o2->s, __builtin_offsetof(struct settings, sort));
        s.sort = SORT_NEAREST;
    } else if (!memcmp(v.magic, SAVE_MAGIC_V1, 4) && o1->sum == checksum(o1, sizeof(*o1) - sizeof(o1->sum))) {
        memcpy(&s, &o1->theme, __builtin_offsetof(struct settings, sound));
        s.sound = SOUND_ON;
        s.sort = SORT_NEAREST;
    } else {
        return FALSE;
    }
    if (s.theme >= THEME_COUNT || s.range_idx >= RANGE_COUNT || s.tag_mode >= TAGS_MODES ||
            s.sound >= SOUND_MODES || s.sort >= SORT_MODES || s.ambient >= AMBIENT_MODES || s.wx >= WX_MODES)
        return FALSE;
    s.icao[4] = 0;
    s.relay[RELAY_LEN] = 0;
    settings = s;
    return TRUE;
}

bool settings_save(void)
{
    struct saved v;

    memcpy(v.magic, SAVE_MAGIC, 4);
    v.s = settings;
    v.sum = checksum(&v, sizeof(v) - sizeof(v.sum));
    return mw_flash_sector_erase(SAVE_SECTOR) == MW_ERR_NONE &&
           mw_flash_write(SAVE_SECTOR * 4096, (u8 *)&v, sizeof(v)) == MW_ERR_NONE;
}
bool settings_dev_suppressed(void)
{
    const u8 *p = mw_flash_read(MARK_SECTOR * 4096, 6);

    return p && !memcmp(p, MARK_MAGIC, 4) && (p[4] << 8 | p[5]) == ROM_CHECKSUM;
}

bool settings_reset(enum settings_reset what)
{
    u8 mark[6] = { 'M', 'W', 'R', 'N', ROM_CHECKSUM >> 8, ROM_CHECKSUM & 0xFF };
    bool ok = mw_flash_sector_erase(MARK_SECTOR) == MW_ERR_NONE &&
              mw_flash_write(MARK_SECTOR * 4096, mark, sizeof(mark)) == MW_ERR_NONE;

    if (what != RESET_WIFI) ok = ok && mw_flash_sector_erase(SAVE_SECTOR) == MW_ERR_NONE;
    /* The firmware only takes FACTORY_RESET while idle (idleCmdMask), not while joined to an AP. */
    if (what != RESET_SETTINGS) {
        mw_ap_disassoc();
        ok = ok && mw_factory_settings() == MW_ERR_NONE;
    }
    return ok;
}
#else
bool settings_load(void) { return FALSE; }
bool settings_save(void) { return FALSE; }
bool settings_dev_suppressed(void) { return FALSE; }
bool settings_reset(enum settings_reset what) { (void)what; return FALSE; }
#endif
