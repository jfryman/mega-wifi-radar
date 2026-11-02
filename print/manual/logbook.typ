#import "lib.typ": *

= The Logbook

Your relay keeps a logbook of everything that flies within 25 nm of your home airport, all day,
whether the cartridge is on or not. The day starts at midnight, airport time. The fifth tab,
*LOGBOOK* (press *B* from *NEAREST*), shows it in the panel; *C* opens the whole page.

#shot("logpage", callouts: ((1, 3, 36), (2, 3, 76), (3, 3, 108), (4, 122, 204)))
#v(1mm)
#key(1)[Today][How many aircraft came through and how many different types, and the busiest
  hour (by aircraft arriving in range). *LOGGED FROM* says when logging began if the relay
  started after midnight.]
#key(2)[Records][The fastest (ground speed), the highest (flight level: *FL380* is 38,000 ft) and
  the one that stayed in range longest.]
#key(3)[Rare today][Military aircraft (*MIL*), aircraft on adsb.fi's list of interesting ones
  (*FLAG*), and unusual types for a regional airport (*TYPE*): a C-17, an A380, a B-52, a P-51...
  newest first, with the time it first appeared.]
#key(4)[Close][*C* or *B*.]

#pagebreak()
== Rare aircraft alerts

When a rare aircraft comes within 25 nm, the radar says so the way it announces a flyover: a box
over the scope and a chime, for example *MILITARY! R08139*, *BOEING-VERTOL CH-47 CHINOOK*,
*21 NM NW 2,775FT*. Each aircraft is announced once a day. Press *C* for its details while the box
is up.

== Good to know

- The relay logs every home airport a cartridge has asked about, every 10 seconds, for as long as
  it runs. Keep it running (see _The Relay_) and the logbook covers the whole day.
- Aircraft whose owners asked not to be tracked publicly (the FAA's LADD list and private ICAO
  addresses) are never logged.
- The log stays on the computer running the relay and is never shared. adsb.fi's data is for
  personal, non-commercial use.
