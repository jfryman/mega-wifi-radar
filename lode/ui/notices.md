# Flyover notices (`src/notice.c`, `src/chime.c`; relay `notice.go`, `mqtt.go`)

When a plane passes over the owner's house, the cart shows a short notice and plays a chime. The trigger is the
owner's `atc-mqtt` service (FR24, 500 m geofence); the relay subscribes to its MQTT topic, composes the text
and serves it ([../relay/relay.md](../relay/relay.md)). Without a broker the relay never has a notice, and the cart
behaves as before.

```mermaid
sequenceDiagram
  participant B as MQTT atc/status
  participant R as relay
  participant C as cart
  B->>R: entering_flights[]
  R->>R: match ICAO, compose 3 x 26 chars, ring (TTL 30 s)
  C->>R: /v1/frame?...&n=<last seen> (or /v1/track in cockpit mode)
  R-->>C: hflags AR_H_NOTICE (kflags AR_K_NOTICE)
  C->>R: /v1/notice?after=<last seen>
  R-->>C: struct ar_notice (88 B), id becomes "last seen"
  C->>C: chime_play; banner (radar) or HUD (cockpit)
```

## Contract (v4, [../architecture/contract.md](../architecture/contract.md))
- `n=` on `/v1/frame` and `/v1/track` = the last notice id seen (0 = none yet). The relay sets the flag when a
  live notice is newer (ids are u16 from a random start, wrap, never 0: newer means `(s16)(id - seen) > 0`).
- `/v1/notice?after=` → `struct ar_notice`: `"AN"`, ver, `nflags` (`AR_N_ICAO`, `AR_N_EMERG`), `id`, `icao[3]`,
  `secs` (15), `text[78]` = 3 lines of `AR_NOTICE_COLS` (26), upper-case ASCII. Id 0 = gone meanwhile.
- Stateless relay: what a cart has seen lives only in the cart (`feed_relay.c` `notice_seen`).

## Cart flow (`notice.c`, one notice at a time)
- `feed_fetch` / `feed_track` record the flag; `notice_check()` (after a radar poll; in cockpit mode in a strip's
  slot instead of a strip) fetches it, stores a copy, `chime_play()`. A newer notice replaces the current one.
- **Radar** (`notice_radar_tick`, every frame): pending + detail pop-up closed → `show()`: select the aircraft if
  matched (`select_it`), write window rows 0-5, `radar_popup_rows(NOTICE_ROWS)`. Closes after `secs`; B closes it
  (`main.c` takes B before zoom); C selects the banner's aircraft and opens the detail pop-up, which takes the
  window over and ends the banner. Pending notices wait while the pop-up is open, up to `WAIT_SECS` (30).
  Cockpit entry closes the banner; setup drops any notice.
- **Cockpit** (`hud_notice` in `cockpit.c`): status slot `OVERHEAD!` (amber), and a **ticker** on row 25 (the credit
  row, amber): the three lines joined (space runs squeezed, three spaces between lines) scroll in from the right and
  across once, then the credit comes back (`secs` doesn't apply: a pass takes (len + 38) / 12 s, ~7 s). The window
  plane can't scroll, so it steps a character every `TICKER_FRAMES` (5) by `vtimer`: even speed at 16-22 fps, and
  the row is only redrawn when the position changes. Riding the notice's aircraft: `OVER HOME!` / `YOU ARE OVER HOME!`
  as the first line. (Owner's call, 2026-11-01: a scrolling ticker, not pages.)

```
row 0  (blank: overscan)
row 1   --------------------------          PAL1
row 2   LOOK UP!   SWA2011                  PAL2 (red if AR_N_EMERG), blinks PAL0 for 2 s
row 3   SOUTHWEST   B737                    PAL0
row 4   1,250FT 160KT                       PAL0
row 5   C DETAILS         B DISMISS         PAL1 ("C DETAILS" only with AR_N_ICAO)
```

## Renderer: a window of N rows (`radar.c`)
`popup_want` / `popup_on` are row counts: `PANEL_ROWS` (28, detail pop-up), `NOTICE_ROWS` (6, banner), 0.
`radar_popup_rows(n)` marks the rows it newly covers dirty (blank ones too, or stale window cells show) and the
V-Int sets `VDP_setWindowVPos(FALSE, popup_on)` once they're written. The side panel (window via HPos) is
unaffected: the VDP shows the window where either split applies. Sprites: none under the full pop-up; under the
banner, blips (and the selection box) with `spr_y < 128 + rows * 8` are skipped (high priority: they'd draw over it).

## Chime (`chime.c`)
- `chime_play()` (main loop) picks the tune: home airport **KBNA → "Music City"** (steel-guitar patch, an original
  lick E4 G4 A4, B4 grace → C5, A4 C5, held E5, ~2 s) — an easter egg for the owner's home; every other airport →
  **generic** (FM bell, C5 E5 G5 C6 a note per 9 frames, ringing ~45). No public catalogue of airport chimes with
  notes exists (searched 2026-11-01), so there's no per-airport table. `settings.chime` = `CHIME_OFF` silences it.
- Loads the patch on FM channels 1-4 with interrupts off; `chime_tick()` keys notes on/off by `vtimer`. It runs
  from the radar V-Int, the cockpit loop and `ui_pad()` (setup's preview: switching CHIME on plays it).
- Patches: `struct fm_patch` = FB/ALG + 4 × (DT/MUL, TL, RS/AR, D1R, D2R, D1L/RR), OP1..OP4 at register offsets
  0, 8, 4, 12. Pitch: F-number table for block 4 (A4 = 1083), block = octave.
- **Lesson (2026-11-01): the YM2612 is in the Z80's window (`$A04000`).** The 68000 must hold the Z80 bus
  (`Z80_getAndRequestBus` / `Z80_releaseBus`) around every write, or the writes vanish: the first build was silent.
  SGDK's null Z80 driver never touches the chip, so no sound driver is needed. In the V-Int, `chime_tick` only takes
  the bus when a note is due; its divisions compile to 16-bit ops (no `jsr __`).

```c
static const struct tune generic = { &bell, 4, { { 0, 45, 12 }, { 9, 45, 16 }, { 18, 45, 19 }, { 27, 60, 24 } } };
/* { start frame, length frames, semitones above C4 } */
```

## Verifying
- `make sim`: the fake feed raises a notice about the nearest airborne aircraft 20 s after the first frame, then
  every 90 s (`feed_fake.c`). `print/shots.txt` `notice | 34` is the manual's shot.
- Sound: `SIM_WAV=out/x.wav scripts/sim out/sim.bin 36` records MAME's mix. The pitch check (2026-11-01) took, at
  each scheduled onset, the spectrum after minus before and scored semitone candidates by their first 4
  harmonics, rejecting candidates with no energy at the fundamental (the bell's ×2 partial, the guitar's strong
  2nd harmonic) and skipping the subtraction for a re-struck pitch. Both tunes: every note right.
- Relay end to end: `make mqtt-start relay-start mqtt-pub`, then `curl '/v1/frame?...&n=0&v=4'` (hflags 0x04) and
  `curl '/v1/notice?after=0&v=4' | xxd`.

Related: [../plans/flyover-notices.md](../plans/flyover-notices.md), [screen-layout.md](screen-layout.md),
[detail-popup.md](detail-popup.md), [cockpit.md](cockpit.md), [config-screen.md](config-screen.md).
