# Plan: flyover notices (0.4.0, contract v4)

**Status (2026-11-01): built and verified in the emulator and against the dev relay; waiting for the real-cart
check and the 0.4.0 release.** How it works now: [../ui/notices.md](../ui/notices.md) (cart, contract, chime) and
[../relay/relay.md](../relay/relay.md) (MQTT client, env config, ring, composer, ICAO match, `/v1/notice`).

## Goal
When a plane passes within ~500 m of the owner's house, the cart shows a short, playful notice and plays a chime:
a banner over the top of the scope in radar mode, a non-blocking HUD line in cockpit mode. The trigger is the
owner's `atc-mqtt` service; the relay subscribes to its MQTT topic. Off unless the relay is given a broker.

```mermaid
flowchart LR
  ATC[atc-mqtt] -- atc/status --> BRK[(broker)] --> RL[relay] -- n= flag + /v1/notice --> CART[cart]
  BRK --> VB[Vestaboard]
```

## Done when
| # | Criterion | State |
|---|---|---|
| 1 | Radar banner within ~5 s, relay text, closes after `secs`, B early, C → that aircraft's details | emulator ✓, relay ✓, cart ✓ |
| 2 | Cockpit: `OVERHEAD!` + a ticker along the bottom, the view never stops | emulator ✓; cart ✓ with pages, **ticker: to check** |
| 3 | Generic chime; KBNA plays the Nashville lick; `CHIME OFF` silences it | WAV pitch check ✓ both; **owner signs off the lick by ear** |
| 4 | A relay without a broker behaves as 0.3.x (apart from v4); idle polling adds no requests | ✓ (the flag rides on existing polls) |
| 5 | `go test`, contract test + lock, `make release` + ram-check, `make sim`, cart ROM boots in MAME | ✓ |
| 6 | Docs, manual, CHANGELOG, lode; 0.4.0 tagged | docs/manual/CHANGELOG/lode ✓; **tag: after the cart check** |

## Left to do
1. ~~Flash the cart~~ (done: `a81f738`, CRC32 `85958703`). Test on it; the dev relay on the Mac already runs v4 with the
   local broker (`make mqtt-start relay-start`). On the cart: `make mqtt-pub` → banner + chime (KBNA lick at home);
   C on the banner; B; a notice during cockpit mode; setup CHIME OFF / ON (preview); settings survive the update
   (the `MWR1` → `MWR2` migration).
2. Owner listens to the KBNA lick and the generic chime; adjust notes/patches in `src/chime.c` if wanted.
3. Production: point the relay at the cluster broker (`MQTT_BROKER_HOST/PORT/USERNAME/PASSWORD`, atc-mqtt's names)
   when it moves into the cluster. No code change.
4. Tag 0.4.0 (relay and cart from the same tag; CHANGELOG date).

## Decisions (2026-11-01)
Banner at the top of the scope; the relay composes the text; one generic chime for every airport plus a KBNA
easter egg (no public list of airport chimes exists); FM (YM2612) voice; minimal stdlib MQTT client; env config
with atc-mqtt's variable names; contract v4 with `n=` + a notice flag (no extra request when idle); cockpit scrolls
the notice as a ticker along the bottom row (the owner's choice after seeing pages on the cart).

Related: [roadmap.md](roadmap.md).
