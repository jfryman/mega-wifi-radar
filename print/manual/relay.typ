#import "lib.typ": *

= Before You Begin: The Relay

Your Genesis has 64 kilobytes of memory. That's far too little to read the raw aircraft feeds the
internet provides, and it can't use the encrypted connections they need. So a small program called
the *relay* runs on a computer on your network. It fetches the traffic around your airport once a
second, shrinks it to a tiny radar frame (about the size of this paragraph), and hands it to the
cartridge every time the sweep passes north.

== Starting the relay

Any computer on your network that stays on will do, including a Raspberry Pi. The cartridge
*finds the relay by itself*, because the relay announces itself on your network (mDNS).

#table(columns: (auto, 1fr), stroke: none, inset: (x: 1.4mm, y: 1.2mm),
  fill: (_, y) => if calc.odd(y) { rgb("#f1f1f3") } else { none },
  text(..cond, weight: "bold")[Linux / Pi],
  block(fill: ink.black, inset: 1.8mm, width: 100%, text(font: font.pixel, size: 4.6pt, fill: ink.phosphor,
    [docker run -d --name mwradar-relay \ #h(3mm) --restart unless-stopped --network host \ #h(3mm)
    ghcr.io/jfryman/mega-wifi-radar-relay])),
  text(..cond, weight: "bold")[Mac],
  [From the project's source: *make relay-start*. Your Mac announces the relay for its container.],
)

If your network blocks these announcements, type the relay's address under *RELAY* in Setup instead:
*#"http://"* plus the computer's name or IP address plus *:8080*, for example
*#"http://radar-pi.home.lan:8080"*.

#note[Fair use][Traffic comes from the adsb.fi open data feed, which is free for personal,
non-commercial use. The panel always credits it. Please keep it that way.]
