# Tabs + spotter's logbook (active plan, 0.5.0, decided 2026-11-02)

Supersedes [ideas/spotters-logbook.md](ideas/spotters-logbook.md) (kept for the background notes until this ships).

## Decisions (owner, 2026-11-02)
- **Relay logs all the time it runs**, to a local **SQLite** DB (`RELAY_DB`), and reads it back as needed. First
  relay dependency: `modernc.org/sqlite` (pure Go: keeps the static amd64/arm64 cross-compile and distroless image).
- **Homes:** every home a cart asks about (frame `lat,lon`) is remembered in the DB and logged continuously from then
  on (background poll every 10 s, through the source's 1 req/s limiter); dropped after 30 days unseen.
- **Day = local midnight at the home airport.** Time zones: an airport → IANA zone table generated at build time
  (nearest GeoNames city, CC-BY: credit it); `time.LoadLocation` with `time/tzdata` embedded.
- **Controls remap** (radar):

  | Button | Before | Now |
  |---|---|---|
  | ◀ / ▶ | tags ALL/SEL/OFF | zoom out / in |
  | A / B | zoom in / out | next / previous **tab** (pop-up open: A cockpit, B close, as before) |
  | ▲ / ▼ | list selection | the same, in list tabs |
  | C tap | details | details (in LOGBOOK: the full logbook page) |
  | C hold | sort order | unused |

  Tag mode moves to setup only (TAGS row). X theme, Y cockpit, Z weather, START setup unchanged.
- **Tabs** (side panel, the list area): NEAREST · LOWEST · HIGHEST · A-Z · LOGBOOK. Z-A is gone (a saved Z-A loads
  as A-Z). `settings.sort` holds the tab.
- **Rare:** military (`dbFlags` & 1), interesting (& 2), or a curated type list; PIA (& 4) / LADD (& 8) aircraft are
  never logged or shown. A rare one entering range → a notice through the existing ring, once per aircraft per day.
- Release **0.5.0**, contract v7.

## adsb.fi facts (checked 2026-11-02, 100 nm around KBNA: 128 aircraft)
`dbFlags`: absent 101, 8 (LADD) 19, 1 (military) 7, 4 (PIA) 1. Military near BNA is routine (ANG C-130s), hence the
per-aircraft-per-day throttle. `t` (type) present on most.

## Steps
1. Relay: tz table generator, SQLite log (homes, sightings, day rollups), background poller, tests on replayed
   snapshots.
2. Contract v7: `/v1/log` → `struct ar_log` (counts, records, rare list); rare notices.
3. Cart: control remap, tabs in the panel, LOGBOOK tab (compact) + full page via the pop-up rows; RAM: fetch into
   `frame_buf` after ingest, render to text, keep no body (heap has 267 B spare).
4. Setup TAGS only, GUIDE/CONTROLS pages, manual (controls, scope, a logbook section), README, shots.
