## 0.1.1 (Unreleased)

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
