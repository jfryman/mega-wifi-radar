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
  R->>R: match ICAO, compose 3 x 24 chars, ring (TTL 30 s)
  C->>R: /v1/frame?...&n=<last seen> (or /v1/track in cockpit mode)
  R-->>C: hflags AR_H_NOTICE (kflags AR_K_NOTICE)
  C->>R: /v1/notice?after=<last seen>
  R-->>C: struct ar_notice (82 B), id becomes "last seen"
  C->>C: chime_play; notice box (radar) or ticker (cockpit)
```

## Decisions (owner, 2026-11-01)
A centred box on the radar (ToeJam & Earl style; a top banner came first), a ticker in cockpit mode (pages came
first), 24-character lines composed by the relay, one generic chime plus a KBNA easter egg (no public catalogue of
airport chimes exists), FM voice, a stdlib MQTT client with atc-mqtt's variable names, `n=` + a flag instead of an
extra poll. Shipped in 0.4.0.

## Contract (v4, [../architecture/contract.md](../architecture/contract.md))
- `n=` on `/v1/frame` and `/v1/track` = the last notice id seen (0 = none yet). The relay sets the flag when a
  live notice is newer (ids are u16 from a random start, wrap, never 0: newer means `(s16)(id - seen) > 0`).
- `/v1/notice?after=` → `struct ar_notice`: `"AN"`, ver, `nflags` (`AR_N_ICAO`, `AR_N_EMERG`), `id`, `icao[3]`,
  `secs` (15), `text[72]` = 3 lines of `AR_NOTICE_COLS` (24), upper-case ASCII. Id 0 = gone meanwhile.
- Stateless relay: what a cart has seen lives only in the cart (`feed_relay.c` `notice_seen`).

## Cart flow (`notice.c`, one notice at a time)
- `feed_fetch` / `feed_track` record the flag; `notice_check()` (after a radar poll; in cockpit mode in a strip's
  slot instead of a strip) fetches it, stores a copy, `chime_play()`. A newer notice replaces the current one.
- **Radar** (`notice_radar_tick`, every frame): pending + detail pop-up closed → `show()`: select the aircraft if
  matched (`select_it`), `radar_box_open(3 lines + hint, palettes)`. After `secs` it shrinks shut
  (`radar_box_close(FALSE)`); B does the same (`main.c` takes B before zoom); C selects the box's aircraft, removes
  the box at once (`close(TRUE)`: it'd be left on plane A under the pop-up) and opens the detail pop-up. Pending
  notices wait while the pop-up is open, up to `WAIT_SECS` (30). Cockpit entry and setup remove it at once.
- **Cockpit** (`hud_notice` in `cockpit.c`): status slot `OVERHEAD!` (amber), and a **ticker** on row 26 (the credit
  row, amber): the three lines joined (space runs squeezed, three spaces between lines) scroll in from the right and
  across once, then the credit comes back (`secs` doesn't apply: a pass takes (len + 38) × 7 / 60 s, ~10 s). The window
  plane can't scroll, so it steps a character every `TICKER_FRAMES` (7, ~8.5 a second; 5 was a bit fast on the cart) by `vtimer`: even speed at 16-22 fps, and
  the row is only redrawn when the position changes. Riding the notice's aircraft: `OVER HOME!` / `YOU ARE OVER HOME!`
  as the first line. (Owner's call, 2026-11-01: a scrolling ticker, not pages.)

```
rows 11-17, cols 1-26 (centre 14, 14), on plane A; the side panel (window) stays clear of it
 .------------------------.    rounded 2-px border, PAL1 colour 15 on opaque black (9 tiles: corners, edges, fill)
 | LOOK UP!   SWA2011     |    PAL2 (red if AR_N_EMERG), blinks PAL0 for 2 s
 | SOUTHWEST   B737       |    PAL0
 | 1,250FT 160KT          |    PAL0
 |                        |
 | C DETAILS     B DISMISS|    PAL1 ("C DETAILS" only with AR_N_ICAO)
 '------------------------'
```

## Renderer: the notice box (`radar.c`, owner's choice 2026-11-01: centred, ToeJam & Earl style)
The window plane can only cover screen edges, so the box is drawn on **plane A** (which holds only text: tags, ring
labels, compass; trail dots are on plane B). The V-Int's `box_tick()`:
- **Animation:** `box_target` (0 shut, 4 open) vs `box_cur`; one step per `BOX_FRAMES` (2): rectangles 4×3, 12×5,
  20×7, 26×7 around the centre, shut in reverse. A step writes the union of the old and new rectangles (`box_rows`:
  new cells get frame tiles, cells left behind get 0), rows 0-3 one frame and 4-6 the next (V-Int budget).
  Emulator snapshots can land between the two halves; at 60 fps it reads as growing (~⅛ s each way).
- **Text** once open: one row a frame with the opaque font (`box_text_dirty` bits); `radar_box_pal` re-colours a row.
- **Tags:** opening marks the open rectangle in `grid` (`box_occupy(1)`) and erases tags inside it (dirty → re-placed
  outside, since `grid_free` sees the cells taken). Fully shut: `box_occupy(-1)`, `draw_labels()` (the compass W/E and
  a ring label sit inside it), `req_retag`. A zoom wipes `grid` and redraws labels: the rescale path then re-occupies
  and redraws the box.
- **Sprites:** blips touching the current rectangle are skipped (high priority: they'd draw over plane A).
- The detail pop-up's window is plain on/off again (`radar_popup`); nothing else shares it.

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
  every 90 s (`feed_fake.c`). `print/shots.txt` `notice | 34` is the manual's shot (the box open).
- Sound: `SIM_WAV=out/x.wav scripts/sim out/sim.bin 36` records MAME's mix. The pitch check (2026-11-01) took, at
  each scheduled onset, the spectrum after minus before and scored semitone candidates by their first 4
  harmonics, rejecting candidates with no energy at the fundamental (the bell's ×2 partial, the guitar's strong
  2nd harmonic) and skipping the subtraction for a re-struck pitch. Both tunes: every note right.
- Relay end to end: `make mqtt-start relay-start mqtt-pub`, then `curl '/v1/frame?...&n=0&v=4'` (hflags 0x04) and
  `curl '/v1/notice?after=0&v=4' | xxd`.

Related: [../relay/relay.md](../relay/relay.md), [screen-layout.md](screen-layout.md),
[detail-popup.md](detail-popup.md), [cockpit.md](cockpit.md), [config-screen.md](config-screen.md).
