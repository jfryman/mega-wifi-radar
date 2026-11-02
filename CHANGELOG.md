## 0.6.1 (Unreleased)

FEATURES:

  * **cockpit:** runways. Below 500 m (1,600 ft) above the ground, the view keeps one close-up scale all the way
    to the runway and through the rollout, and draws every runway of the big airports nearby: concrete grey by
    day, lit after dusk. On final it comes up out of the horizon about a mile and a half out.
  * **relay:** `-record` saves the live feed around a point, `-cut` trims a recording, and `-replay` (or
    `scripts/relay replay FILE`) serves one as if it were live, so the cartridge can fly a recorded arrival on
    demand. Two real KBNA arrivals ship in `relay/testdata/arrivals/`.

IMPROVEMENTS:

  * **cockpit:** near the ground the camera rides at least a third of a cell (20 m) up, and flat ground is
    textured in fields, so the view keeps moving instead of turning into single-colour slabs.
  * **cockpit:** the city-light cells are plain ground by day at close range (they buried the runways in grey).

BUG FIXES:

  * **cockpit:** the view was mirrored and stretched sideways at every heading (an overflow in the renderer's
    column step), which made the terrain smear and slide; it now matches the ground, and turns look right.
  * **cockpit:** aircraft flew 100 ft low around Nashville: GPS altitude is height above the WGS84 ellipsoid, and
    the relay now takes the geoid (EGM96) off it; barometric altitude uses the crew's altimeter setting when sent.
  * **cockpit:** the camera can no longer end up inside the ground (the whole view one colour) when a reported
    altitude is low or the cartridge flies on between fixes.

## 0.6.0 (November 2, 2026)

BACKWARDS INCOMPATIBILITIES:

  * **relay:** the contract is now version 8 (each aircraft record carries its type). A 0.6.0 cartridge needs the
    0.6.0 relay (and the other way round); a mismatch is reported at boot or as `RELAY VER?`, as before.

FEATURES:

  * **radar:** in the STARS and ERAM themes, a data block's second line takes turns, as on a real STARS scope:
    altitude and speed for two seconds, then the aircraft type (B738, CRJ7, H60) for two.

IMPROVEMENTS:

  * **cartridge:** about 2.7 KB more free RAM (for features to come): the scope's tile map is read from ROM, label
    cells are one bit each, and the boot splash and setup's Wi-Fi list borrow the radar's memory while it's stopped.

## 0.5.0 (November 2, 2026)

BACKWARDS INCOMPATIBILITIES:

  * **radar:** new controls. Left/right zoom (right in, left out); A/B change the panel's tab; tag mode is in setup's
    TAGS row only; holding C no longer does anything.
  * **relay:** the contract is now version 7. A 0.5.0 cartridge needs the 0.5.0 relay (and the other way round); a
    mismatch is reported at boot or as `RELAY VER?`, as before.

FEATURES:

  * **radar:** tabs in the side panel: NEAREST, LOWEST, HIGHEST, A-Z (the old sort orders; Z-A is gone) and LOGBOOK.
  * **logbook:** the relay keeps a spotter's logbook of everything within 25 nm of your home airport, all day, whether
    the cartridge is on or not, in a SQLite file (`RELAY_DB`). Days end at the airport's midnight. The LOGBOOK tab
    shows the day so far (aircraft, types, busiest hour, the fastest, highest and longest-in-range aircraft, rare
    ones); C opens the full page.
  * **logbook:** rare aircraft (military, adsb.fi's interesting list, unusual types such as a C-17 or an A380) pop up
    like a flyover notice, once per aircraft per day. Aircraft on the FAA's LADD list are never logged.

IMPROVEMENTS:

  * **setup:** the CONTROLS and GUIDE pages describe the new controls and tabs.
  * **manual:** a Logbook section; the controls and scope pages follow the new buttons.
  * **relay:** built with Go 1.26; its first dependency, a pure-Go SQLite (`modernc.org/sqlite`).

## 0.4.5 (November 2, 2026)

BACKWARDS INCOMPATIBILITIES:

  * **relay:** the contract is now version 6. A 0.4.5 cartridge needs the 0.4.5 relay (and the other way
    round); a mismatch is reported at boot or as `RELAY VER?`, as before.

IMPROVEMENTS:

  * **radar:** a video map. The runways of the airports in view are drawn as short solid bars, to scale, and out to
    25 nm your home airport's runways get dashed extended centrelines (10 nm, a tick every mile) that arrivals line
    up on. They cross the range rings unbroken. The sweep passes over them, and over trail dots, without wiping
    them. MAP in setup turns it off.
  * **setup:** the MAP row; the settings now sit in blocks (scope, sound, airport, network).
  * **setup:** GUIDE: four pages on what's on the radar (tags and blips, weather, the video map and reading an
    approach, the side panel), in the theme's own colours, with samples of the weather and map lines.
  * **radar:** the sweep's afterglow is a step shorter (9 shades instead of 10), freeing a colour for the map.
  * **relay:** serves `/v1/map` from OurAirports runway data built in (`tools/runwaygen`).
  * **manual:** a Video Map section: what the lines are, and how to read an approach (300 ft a mile).

## 0.4.4 (November 2, 2026)

BACKWARDS INCOMPATIBILITIES:

  * **relay:** the contract is now version 5. A 0.4.4 cartridge needs the 0.4.4 relay (and the other way
    round); a mismatch is reported at boot or as `RELAY VER?`, as before.

IMPROVEMENTS:

  * **radar:** weather. Rain and storms from NOAA's NEXRAD radars show behind the traffic as dotted cells, the
    way a STARS scope draws them: sparse for light rain, denser for moderate, nearly solid for heavy. New every
    five minutes and after each zoom; tags, rings and the sweep stay on top. US only (Iowa Environmental Mesonet's
    composite); the panel credits `IEM`. WEATHER in setup or Z on a six-button pad turns it off.
  * **setup:** the WEATHER row; WI-FI and RELAY now sit together.
  * **relay:** serves `/v1/wx` (`-wx` / `RELAY_WX`; empty turns weather off).

## 0.4.3 (November 1, 2026)

IMPROVEMENTS:

  * **radar:** ambient mode. Set AMBIENT in setup to 1, 5 or 15 minutes and, left alone that long, the radar
    tours flights by itself: it picks one (an emergency first, then low traffic near your airport), shows its
    details, rides along in the cockpit view for 75 seconds, and comes back; a minute later, another. The panel
    says `NET OK AUTO`. Any button takes over again, and that press does nothing else.
  * **setup:** the AMBIENT row; CONTROLS, RESET, SAVE & EXIT and EXIT now sit together at the bottom.
  * **cockpit:** towns show: cells under city lights (NASA's night-lights map) are grey specks by day and warm
    points of light at dusk and night, so an evening arrival into a city looks like one. The relay's
    `RELAY_LIGHTS` points elsewhere or turns it off.
  * **radar:** the STARS and ERAM themes draw tags as data blocks: set off from the target on a short leader line,
    in whichever of eight directions has room, and staying on the same side from sweep to sweep. Classic and the
    other themes keep tags beside the target.

## 0.4.2 (November 1, 2026)

IMPROVEMENTS:

  * **radar:** seven new themes: AMBER (amber phosphor), NIGHT (dim reds for a dark room), MARINE (a ship's
    radar, yellow on navy), ICE (blue-white phosphor), POCKET (a handheld's green LCD), SUNSET (cyan sweep
    over purple) and ERAM (an en-route centre display). Pick one in setup's THEME row, or press X. The manual
    shows them all.
  * **radar:** an emergency alert: when an aircraft squawks 7700, 7600 (radio failure) or 7500, or reports an
    emergency, the notice box opens with the code, where it is and how high, and a two-tone alert sounds
    (SOUND in setup turns it off). Once per aircraft, at most one a minute; flyover notices wait their turn.
  * **radar:** zoom, tags, theme and the traffic list's sort order are remembered when you change them on the
    radar, not only from setup. Saved settings from 0.3 and 0.4 carry over.
  * **cockpit:** the sky follows the real sun where the aircraft is: blue by day, gold at a low sun, orange
    and purple in twilight, and a dark navy night over a dimmed ground. The relay's `RELAY_SKY` fixes one for
    testing.

BUG FIXES:

  * **radar:** coming back from cockpit mode no longer leaves the scope's text half a screen out of place
    (compass letters, ring labels, tags and the notice box shifted down and wrapped round, N below the
    centre). Cockpit mode's page flip could leave the text layer scrolled; the radar now resets it.

## 0.4.1 (November 1, 2026)

IMPROVEMENTS:

  * **boot:** the radio calls pop up in speech bubbles from whoever is talking, the tower or the 787, and
    each one stays up long enough to read. The wordmark moves to the top and the airport fills the rest of the screen; the
    pilot reads the takeoff clearance back at the hold line, then rolls and climbs out behind the logo.
  * **boot:** sound, on the PSG and FM chips: the 787's twin engines (two drones that beat against each other,
    a turbine whine and a deep bass rumble) spool up at the gate, idle through taxi and roar on the
    takeoff roll, fading as it climbs away; each radio call crackles open and shut, with a garbled radio voice
    (higher for the tower, lower for the pilot) while it types.
  * **setup:** the CHIME row is now SOUND and switches the boot sounds and the flyover chime together. Your
    saved choice carries over.
  * **print:** the manual's start-up page shows the new boot screen; the setup and notices pages say SOUND.

## 0.4.0 (November 1, 2026)

BACKWARDS INCOMPATIBILITIES:

  * **relay:** the contract is now version 4. A 0.4.0 cartridge needs the 0.4.0 relay (and the other way
    round); a mismatch is reported at boot or as `RELAY VER?`, as before.
  * **setup:** saved settings move to a new record (`MWR2`) with the chime setting. A 0.3.x record is read and
    upgraded automatically, so nothing is lost; a 0.3.x cartridge can't read the new one back.

FEATURES:

  * **notices:** flyover notices. With the relay connected to an MQTT flyover feed (such as the
    `atc-mqtt` service's `atc/status`), a box pops open over the middle of the scope for 15 seconds when a
    plane passes over home, then shrinks shut: a headline and callsign, airline and type, route, altitude
    and speed. When the relay finds the aircraft in its feed, it's selected and C opens its details; B
    dismisses it. In the cockpit view it scrolls along the bottom of the HUD instead, without covering
    the view.
  * **notices:** a chime on the Genesis's FM sound chip plays as a notice arrives; setup's new CHIME row turns
    it off. Some home airports have a tune of their own.
  * **relay:** subscribes to an MQTT broker (`MQTT_BROKER_HOST`, `MQTT_TOPIC`...), turns entering flights into
    notices (matched to the adsb.fi aircraft when it can) and serves them on `/v1/notice`. Off unless a broker
    is configured. Built-in client, no new dependencies.
  * **relay:** every server setting can come from an environment variable (`RELAY_*`, `MQTT_*`), for
    containers and Kubernetes. The MQTT names match `atc-mqtt`'s, so both can share one secret.

IMPROVEMENTS:

  * **cockpit:** a tidier HUD: the aircraft's type and operator next to its callsign, an instrument strip
    (altitude, ground speed, vertical speed, heading, bank) in fixed columns so the numbers don't jump around, and
    its bearing and distance from your home airport.
  * **print:** the manual has a *Flyover Notices* page, the setup page covers CHIME, and the cockpit page shows the new HUD.
  * **docs:** `docs/flyover.md` explains connecting a flyover feed: relay settings for Docker and Kubernetes,
    the message format the relay accepts, a test command and a troubleshooting table. The manual's relay page
    points to it.
  * **dev:** `make mqtt-start` runs a local MQTT broker the dev relay uses by itself; `make mqtt-pub`
    publishes a sample flyover. `scripts/sim` can record the sound (`SIM_WAV`).

## 0.3.0 (October 31, 2026)

BACKWARDS INCOMPATIBILITIES:

  * **relay:** the contract is now version 3. A 0.3.0 cartridge needs the 0.3.0 relay (and the other way
    round); a mismatch is reported at boot or as `RELAY VER?`, as before.

FEATURES:

  * **cockpit:** a cockpit view. Press A in the detail pop-up (or Y on a 6-button pad) to ride along with
    that aircraft: the console draws the real terrain around it, flying smoothly between position updates
    and banking and pitching with the aircraft, with its callsign, altitude, speeds, heading and bank
    underneath. New terrain streams in ahead of the aircraft as it flies, so the view only pauses for
    a moment now and then. B goes back to the radar with the aircraft still selected.
  * **relay:** `/v1/terrain`, `/v1/track` and `/v1/strip` serve the cockpit view: a terrain patch around the aircraft,
    built from public elevation tiles (AWS Terrain Tiles), and the aircraft's position in it. The relay
    needs HTTPS access to `s3.amazonaws.com` for terrain; without it the view shows flat ground.

IMPROVEMENTS:

  * **print:** the manual has a *Cockpit View* page, the controls and troubleshooting pages cover
    it, and the credits carry the elevation data's attribution (also on the cockpit screen).

BUG FIXES:

  * **radar:** the cartridge now recovers when the relay's computer goes away mid-session (lid closed,
    power lost) and comes back. Before, the module's replies fell out of step with the console and the
    panel stayed on `STALE` until a power cycle. After three failed polls the cartridge restarts the
    module, rejoins Wi-Fi and finds the relay again (`RECONNECT` on the panel), retrying every 15–60 s
    while the relay stays away. Typed relay addresses recover too.
  * **net:** a failed request now really resets the module's HTTP client (SGDK sent the wrong command).

## 0.2.1 (October 29, 2026)

IMPROVEMENTS:

  * **print:** the clamshell cover is now a full wrap: a back with screenshots, features and
    what you need, a 26 mm spine with the title, and the front. `cover-a4.pdf` puts it on A4
    landscape for home printing, with cut and fold marks (the wrap is too wide for Letter).

## 0.2.0 (October 29, 2026)

BACKWARDS INCOMPATIBILITIES:

  * **radar:** new controls. The D-pad no longer pans the scope: UP/DOWN select aircraft in the
    traffic list, LEFT/RIGHT cycle the tags (all / selected / off), and holding C changes the
    list's sort order instead of the tags.
  * **relay:** the relay and the cartridge now come as a pair: use the relay from the same release
    as your cartridge. A 0.2.0 cartridge won't use a 0.1.x relay (or the other way round), and it
    says so at boot (*RELAY MISMATCH: RUN RELAY v0.2.0*) or on the radar (`RELAY VER?`) instead of
    failing with a generic error. Update the relay image along with the cartridge.

FEATURES:

  * **radar:** a traffic list on the side panel, replacing the on-screen control help: every
    tracked aircraft's callsign and distance, with the selection highlighted, and emergencies
    always on top. Hold C to sort by nearest, A-Z, Z-A, lowest or highest altitude.
  * **radar:** X on a 6-button pad cycles the theme. It's a shortcut only: the theme is also in
    Setup, so nothing needs a 6-button pad.
  * **setup:** a CONTROLS page lists the radar and setup controls.
  * **radar:** tap C for a detail pop-up on the selected aircraft: registration, type, model,
    operator and year, altitude, vertical speed, GPS and selected altitude, selected heading,
    altimeter setting, speed, track, squawk (with its meaning), category, distance and bearing.
    It updates every sweep, follows UP/DOWN, and C or B closes it.
  * **relay:** `GET /v1/detail?icao=…` serves one aircraft's details for the pop-up, from the
    latest upstream snapshot (or a single-aircraft upstream request).

IMPROVEMENTS:

  * **build:** `make release` and `make sim` fail if a ROM leaves under 8 KiB of RAM for SGDK's
    start-up allocations, which would otherwise hang the console on a black screen. SGDK's unused
    DMA copy buffer is cut from 8 KiB to 2 KiB to make room.
  * **build:** `relay/contract.lock` ties `src/arframe.h` to its contract version, so the build
    fails if the contract changes without a version bump (`make contract-lock` records a new one).
  * **relay:** logs its release and contract version at start-up, and logs requests from a
    cartridge at another version.

## 0.1.3 (October 29, 2026)

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
  * **boot:** after connecting in the first-boot setup, SAVE & EXIT no longer loops back to
    *Wi-Fi failed*. The module was already joined, and the firmware refuses a second join, so the
    cart now uses the existing connection.
  * **setup:** connecting to Wi-Fi always saves the network and reads it back. It used to skip the
    save when the module's working copy already matched, which could leave it unsaved. Failures
    name the step (AP CFG, FLASH SAVE, READ-BACK...).

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
