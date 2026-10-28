#ifndef AIRPORTS_H
#define AIRPORTS_H

/* Home-airport table for the config screen (generated: tools/airportgen). */
#include <genesis.h>
#include "airports_count.h"

struct airport {
    char icao[5];
    s32 lat_e4, lon_e4;         /* 1e-4 degrees */
    char name[AIRPORT_NAME_LEN + 1];
};

extern const struct airport airports[AIRPORT_COUNT];

/* Index of an ICAO ident, or -1. */
s16 airport_find(const char *icao);

#endif
