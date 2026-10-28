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
- **Released:** v0.1.0 (radar), v0.1.1 (print assets), v0.1.2 (boot splash, label/tag bug fixes).
  **0.1.3 is unreleased** (see `CHANGELOG.md`): mDNS relay discovery (AUTO RELAY). Built and pushed, and the
  discovery spike is verified on the cart, but the full AUTO build hasn't been flashed and tested on hardware yet.
- Everything ships from github.com/jfryman/mega-wifi-radar: cart + emulator ROMs, label/cover/manual PDFs, and the relay
  image `ghcr.io/jfryman/mega-wifi-radar-relay` ([../build/ci-release.md](../build/ci-release.md)).
- Verified on the cart: cold-boot detect, Wi-Fi join, relay polling once per sweep, renderer timing, setup + flash
  persistence, the splash, and mDNS discovery (spike).
- The dev relay runs as the `mwradar-relay` container on the dev Mac (:8080), advertised by `dns-sd -P` from
  `scripts/relay start`.

## Next to check on hardware (0.1.3)
- Flash the current ROM. In setup, press C on RELAY (AUTO), SAVE & EXIT, power-cycle: the boot should show
  *request radar service*, then the status line `RELAY 10.x.x.x:8080`.
- Stop the relay (`make relay-stop`) during radar use: after 3 failed polls the cart re-discovers. Restart it and
  the radar should recover without a reboot.

```sh
make flash                     # cart ROM (FlashKit); then cold-boot
make relay-stop && make relay-start   # exercise AUTO re-discovery mid-session
dns-sd -B _mwradar._tcp        # is the relay advertised?
```

## Open decisions / ideas
- [ ] Data path A vs B: relay (current) or a firmware digester ([../architecture/data-path-options.md](../architecture/data-path-options.md)).
- [ ] Relay binaries for macOS/Linux in releases (native runs advertise mDNS themselves; easier than a container on a Mac).
- [ ] Persist the host mDNS advertisement across reboots (launchd / systemd unit); today it's `scripts/relay advertise`.
- [ ] STARS polish: data blocks with leader lines, pan indicator; aircraft type in frame v2 (28-byte record).
- [ ] Sound (PSG engine noise on the splash), non-KBNA fake feeds for emulator demos.

## Decided
SGDK master `9a78a44` in a container; V-Int renderer, so blocking polls never stall the sweep; frame v1 over HTTP GET
once per sweep with Content-Length; relay in Go (stdlib); screen layout and thin-line sweep; settings record `MWR1` in
user_data sector 0; OurAirports large + scheduled airports; print assets in Typst; boot splash driven by boot steps;
relay discovery by mDNS legacy unicast with AUTO default.
