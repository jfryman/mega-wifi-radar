## 0.1.3 (Unreleased)

FEATURES:

  * **relay:** finds itself on the network. The relay advertises `_mwradar._tcp` over mDNS
    (`-mdns`, on by default), and `scripts/relay start` also registers containerized relays with
    the host's mDNS (`dns-sd` on macOS, `avahi-publish` on Linux).
  * **cartridge:** RELAY defaults to **AUTO**. After joining Wi-Fi, the cart finds the relay with one
    mDNS query, and it looks again after three failed polls. A typed URL still pins a relay, and C
    in Setup goes back to AUTO. The boot adds a *request radar service* call, and the status line
    shows the relay in use.
  * **setup:** a RESET row forgets the Wi-Fi networks, the settings, or both (A twice to confirm),
    then restarts into the first-boot flow. On dev builds the compiled-in Wi-Fi and relay stay off
    until a different ROM is flashed, so those flows can be tested.

IMPROVEMENTS:

  * **build:** a relay ↔ cartridge contract check. `src/arframe.h` defines the frame layout,
    constants, HTTP query and mDNS strings, and `relay/contract_test.go` fails CI and `make release`
    if the relay disagrees with any of it, or if either side adds an item the other doesn't know.

BUG FIXES:

  * **setup:** scanning for Wi-Fi from the radar (START → WI-FI) no longer fails while connected:
    the firmware only scans when idle, so the cart leaves the network first, and it rejoins after
    setup if you back out.

BACKWARDS INCOMPATIBILITIES:

  * **cartridge:** an empty RELAY setting now means AUTO (it used to mean "not set"). Saved
    settings with a typed URL keep using it.

## 0.1.2 (October 28, 2026)

FEATURES:

  * **boot:** the text boot screen is now a departure scene. A 787 taxis out beside the control
    tower, holds short and takes off behind the MEGA WIFI RADAR wordmark, which has a sweeping
    shine. Each ATC radio call is a real boot step (module, settings, Wi-Fi, connected). Failures show
    as *hold position* with the reason. START skips ahead.

BUG FIXES:

  * **radar:** after zooming, the ring range labels no longer keep digits from the previous range
    (5 nm used to read `1 20 35 40 50`).
  * **radar:** data tags are no longer placed directly right of a ring or compass label, where the
    two read as one (`S` + `017` looked like `5017`).

IMPROVEMENTS:

  * **manual:** the Range Steps section shows the scope zoomed to 5 nm. Starting Up shows and
    explains the departure.

## 0.1.1 (October 28, 2026)

FEATURES:

  * **print:** a printable cartridge label, clamshell front cover and 16-page instruction manual in
    the early-Genesis style, built from shared art and copy (`make print`, `docs/print.md`). The key
    art is generated from recorded traffic. The manual comes as single pages plus imposed Letter and A4
    booklets, and releases attach all of them.

IMPROVEMENTS:

  * **sim:** the emulator build never includes `src/dev_config.h`, so its screenshots can't show
    local network details.
  * **sim:** several presses of the same button in one `SIM_PRESS` list now all register.

## 0.1.0 (October 28, 2026)

Initial release.

FEATURES:

  * **Radar scope:** ATC-style display of live ADS-B traffic around a chosen airport, with range
    rings, range labels, rim ticks and compass points.
  * **Thin-line sweep:** a rotating sweep line (4.3 s per turn on NTSC) with a phosphor afterglow.
  * **Targets:** blips that update as the sweep passes and fade between hits. Each has a 5-position
    history trail. Stale targets are drawn dim and are dropped after 60 s.
  * **Data tags:** two-line tags (callsign, altitude, ground speed) coloured by altitude band,
    with emergency squawks in red. Hold C to cycle all / selected / off.
  * **Controls:** zoom from 5 to 100 nm (A/B), pan with the D-pad, and select aircraft with C. The
    side panel shows the selected aircraft's details, UTC time, link state and the data source.
  * **Themes:** Classic (green phosphor with sweep) and STARS (no sweep, constant blips).
  * **Setup screen:** Wi-Fi scan and an on-screen keyboard for the password; home airport from
    1,143 large airports with scheduled service; relay URL; theme, range and tag defaults. Settings
    are saved to the ESP32's flash. Setup opens automatically when Wi-Fi or the relay isn't set,
    or when START is held at boot.
  * **Relay:** a Go service that turns adsb.fi JSON into a compact binary frame (v1) for the
    console, with a per-query cache and last-good-frame fallback. Published as
    `ghcr.io/jfryman/mega-wifi-radar-relay` (linux/amd64, linux/arm64).
  * **Emulator build:** replays a recorded feed around KBNA, so it runs without MegaWiFi hardware.
  * **Hardware:** targets the MegaWiFi Rev.C cartridge (TL16C550D, 20 MHz / 1.25 Mbaud) with ESP32
    firmware `rev-c-1250k`. Includes `make flash` (FlashKit MD) and `make esp-flash` (ESP32-C3 over USB-C).
  * **Build:** reproducible container build (gcc 13.2, SGDK master `9a78a44` patched for Rev.C).
    The image is cached on GHCR, and CI builds release ROMs.
