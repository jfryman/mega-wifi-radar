#include "settings.h"
#include "devcfg.h"
#include "theme.h"

const u16 range_steps[RANGE_COUNT] = { 5, 10, 25, 50, 100 };

struct settings settings;

void settings_init(void)
{
    u16 best = 0;

    for (u16 i = 1; i < RANGE_COUNT; i++) {
        s16 d = (s16)range_steps[i] - (s16)devcfg_range_nm;
        s16 b = (s16)range_steps[best] - (s16)devcfg_range_nm;
        if ((d < 0 ? -d : d) < (b < 0 ? -b : b)) best = i;
    }
    settings.theme = THEME_CLASSIC;
    settings.range_idx = best;
    settings.tag_mode = TAGS_ALL;
}
