# Roadmap

```mermaid
flowchart TD
  B[Build: pinned SGDK + Rev.C patch, container] --> N[Net: detect, join, HTTP, chunked]
  N --> R[Relay: Go, v1 compact frame, mDNS]
  F[Frame format v1] --> R
  F --> V[Radar: V-Int renderer, sweep, tags, themes]
  R --> L[Live radar on the cart]
  V --> L
  L --> S[Setup + settings in ESP32 flash]
  L --> P[Boot splash: 787 departure]
  S --> D[AUTO relay discovery]
  L --> G[Public repo, CI, releases, print assets]
```

## Status (current)
- **Released:** v0.1.0 (radar), v0.1.1 (print assets), v0.1.2 (boot splash, label/tag bug fixes),
  v0.1.3 (mDNS relay discovery / AUTO RELAY, setup RESET, relay ↔ cart contract check, Wi-Fi save / first-boot
  join fixes), v0.2.0 (traffic list, new controls, detail pop-up + `/v1/detail`, setup CONTROLS, contract v2 with
version lock-step, start-up RAM check). 0.2.1 is open in `CHANGELOG.md`. The cart runs the v0.2.0 release ROM (CRC32 `DA73BCE4`) and
boots (the first 0.2.0 flash booted to black: [../build/ram-budget.md](../build/ram-budget.md)). **Verified on the cart (2026-10-28):**
  RESET → Everything → first-boot Wi-Fi page → SAVED + CONNECTED → SAVE & EXIT → AUTO relay → radar, and a power cycle
  keeps it. **Verified on the cart (2026-10-29), v0.1.3 ROM:** AUTO re-discovery mid-session (`make relay-stop` while
  the radar runs, then `make relay-start`: the cart searches again after 3 failed polls and recovers without a reboot),
  and START → WI-FI from the radar (scan while joined → leave AP → rejoin after setup). The dev file (`src/dev_config.h`) is removed, so the dev cart runs release-style builds.
- Everything ships from github.com/jfryman/mega-wifi-radar: cart + emulator ROMs, label/cover/manual PDFs, and the relay
  image `ghcr.io/jfryman/mega-wifi-radar-relay` ([../build/ci-release.md](../build/ci-release.md)).
- Verified on the cart: cold-boot detect, Wi-Fi join, relay polling once per sweep, renderer timing, setup + flash
  persistence, the splash, mDNS discovery, setup RESET, first-boot Wi-Fi save + join,
  AUTO relay re-discovery mid-session, and the Wi-Fi scan + rejoin from the radar's setup.
- Verified on the cart (2026-10-29), 0.2.0: the traffic list, ▲▼ selection, hold-C sort, ◀▶ tags, the detail
  pop-up against the real relay (▲▼ while open, C/B close), X on a 6-button pad, setup CONTROLS, and the
  relay-mismatch message.
- The dev relay runs as the `mwradar-relay` container on the dev Mac (:8080), advertised by `dns-sd -P` from
  `scripts/relay start`.

## Not yet checked on hardware
- Nothing outstanding.

```sh
make flash                     # cart ROM (FlashKit); then cold-boot
make relay-stop && make relay-start   # exercise AUTO re-discovery mid-session
dns-sd -B _mwradar._tcp        # is the relay advertised?
```

## Open decisions / ideas
- [ ] Data path A vs B: relay (current) or a firmware digester ([../architecture/data-path-options.md](../architecture/data-path-options.md)).
- [ ] Relay binaries for macOS/Linux in releases (native runs advertise mDNS themselves; easier than a container on a Mac).
- [ ] Persist the host mDNS advertisement across reboots (launchd / systemd unit); today it's `scripts/relay advertise`.
- [ ] STARS polish: data blocks with leader lines.
- [ ] Y / Z on 6-button pads (reserved).
- [ ] Persist the sort order (RAM only today; adding it to `struct settings` would invalidate saved `MWR1` records).
- [ ] Sound (PSG engine noise on the splash), non-KBNA fake feeds for emulator demos.

## Decided
SGDK master `9a78a44` in a container; V-Int renderer, so blocking polls never stall the sweep; compact frame over HTTP GET
once per sweep with Content-Length; relay and cart in lock-step (contract version, 409 on mismatch); no pan, a traffic
list and detail pop-up instead (0.2.0); relay in Go (stdlib); screen layout and thin-line sweep; settings record `MWR1` in
user_data sector 0; OurAirports large + scheduled airports; print assets in Typst; boot splash driven by boot steps;
relay discovery by mDNS legacy unicast with AUTO default.
