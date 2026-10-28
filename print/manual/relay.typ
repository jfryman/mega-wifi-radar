#import "lib.typ": *

= Before You Begin: The Relay

Your Genesis has 64 kilobytes of memory. That's far too little to read the raw aircraft feeds the
internet provides, and it can't use the encrypted connections they need. So a small program called
the *relay* runs on a computer on your network. It fetches the traffic around your airport once a
second, shrinks it to a tiny radar frame (about the size of this paragraph), and hands it to the
cartridge every time the sweep passes north.

== Starting the relay

Any computer that stays on and can run Docker will do, including a Raspberry Pi:

#block(fill: ink.black, inset: 2.5mm, width: 100%, text(font: font.pixel, size: 5pt, fill: ink.phosphor,
  [docker run -d --name mwradar-relay \ #h(4mm) --restart unless-stopped -p 8080:8080 \ #h(4mm)
  ghcr.io/jfryman/mega-wifi-radar-relay]))

The relay's address is *http://* followed by the computer's name or IP address and *:8080*, for
example *http://radar-pi.home.lan:8080*. Use the full name your router gives the computer, or its IP
address. Names ending in *.local* won't work.

#note[Fair use][Traffic comes from the adsb.fi open data feed, which is free for personal,
non-commercial use. The panel always credits it. Please keep it that way.]
