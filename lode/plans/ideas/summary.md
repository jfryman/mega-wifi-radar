# Feature ideas (goals backlog, written 2026-11-01)

Ideas the owner can promote to goals. Each file states the outcome, a design that fits the current code, the
contract and RAM/V-Int costs, risks, and a "done when" checklist. None is scheduled; nothing here is built.
When one is picked: re-read its file against the code (things drift), settle its open questions with the owner,
then move it to `plans/` as the active plan and update it to the current state as it's built.

| Goal | What you get | Where | Contract | Size | Depends on |
|---|---|---|---|---|---|
| *STARS type line* (below) | data blocks alternate line 2 with the aircraft type | both | v8: type in `ar_rec` (24 → 28) | M | v8 release (RAM freed: fits now) |
| [../watch-arrivals.md](../watch-arrivals.md) | ARRIVALS list order, cockpit through touchdown, relay on the Pi | both | v8 flag bit | M-L | cockpit low-altitude fixes |

S = a session or two, M = a few sessions, L = a release of its own.

```mermaid
flowchart LR
  ARR[watch-arrivals] -. arrivals score .-> AMB[ambient mode: built, ui/ambient.md]
  MAP[video maps: built, ui/video-maps.md] -. centrelines .-> ARR
  LOG[logbook: built, ui/logbook.md] -. busy hours .-> ARR
```

## STARS type line (phase 2 of the data blocks)
STARS and ERAM draw tags as data blocks on leader lines ([../../ui/radar-renderer.md](../../ui/radar-renderer.md)). Real
STARS time-shares line 2: altitude + speed, then the type (B38M), every ~2 s. The record has no type (only the
96-byte `ar_detail` does), so it means growing `ar_rec` to 28 bytes (`char type[4]`): ~1.2 KB more RAM (tracks ×2
records + `frame_buf`), and a contract bump, so it belongs in the v8 release, ; the heap (10.8 KiB) now has room. The cart then swaps `tag[1]` on a
timer for tags in leader themes (≤ 2 redraws a frame already bound it). Open: STARS's own format (speed in tens of
knots, 2 digits) or today's `016 174`.

## Suggested order
1. **One contract release (v8)** bundling what needs the relay: arrivals, the STARS type line (v5 weather, v6 video
   maps, v7 tabs + logbook: all built).
   Bundle them so the cart and relay bump `AR_VER` once ([../../architecture/contract.md](../../architecture/contract.md)).

## Shared constraints (from the lode)
- RAM: ~10.8 KiB heap at start-up (after the 2026-11-02 trim; the floor is 8 KiB), minimum 8 KiB ([../../build/ram-budget.md](../../build/ram-budget.md)); big buffers go
  in the mode overlay; boot the cart ROM in MAME after RAM changes.
- V-Int: ~175 of 262 lines used on the radar; measure with `RADAR_DEBUG` ([../../ui/radar-renderer.md](../../ui/radar-renderer.md)).
- Free palette entries: none in PAL0 (11 map, 13 weather); PAL1 7-13, PAL2 1-13, PAL3 5-13 (14 is the background everywhere) ([../../ui/screen-layout.md](../../ui/screen-layout.md)).
- 3-button pad must reach everything; new screens hang off setup.
- Each user-visible change: changelog, manual section, shots ([../../practices.md](../../practices.md)).

Related: [../roadmap.md](../roadmap.md).
