#import "lib.typ": *

= Setting Up

Press *Start* on the radar to open Setup, or hold *Start* while you turn the power on. Setup
also opens by itself if the cartridge can't join Wi-Fi or can't find the relay.

#shot("setup", callouts: ((1, 18, 43), (2, 18, 75), (3, 18, 99), (4, 18, 115), (5, 18, 131), (6, 18, 147), (7, 18, 163)))
#v(1mm)
#grid(columns: (1fr, 1fr), column-gutter: 4mm, row-gutter: 1.6mm,
  key(1)[THEME, RANGE, TAGS][#sym.arrow.l #sym.arrow.r on the D-Button changes them.],
  key(2)[AIRPORT][#sym.arrow.l #sym.arrow.r steps through airports, *C* jumps to the next letter,
    *A* lets you type the four-letter ICAO code (KBNA, KORD, EGLL...).],
  key(3)[WI-FI][*A* scans for networks: pick yours, type its password.],
  key(4)[RELAY][*AUTO* finds the relay by itself. *A* types an address instead; *C* goes back to AUTO.],
  key(5)[CONTROLS][Every button, on screen.],
  key(6)[RESET][Forget Wi-Fi, settings or both, and restart as new.],
  key(7)[SAVE & EXIT][Keeps your changes. *EXIT* drops them.],
)

#pagebreak()
== The keyboard

#align(center, shot("keyboard", width: 80%, callouts: ((1, 42, 90), (2, 42, 139))))
#v(1mm)
#key(1)[Characters][Move with the D-Button and press *A* to type. *B* deletes the last letter.]
#key(2)[SPACE, DEL, OK, CANCEL][On the bottom row. *Start* is a shortcut for OK.]

== Remembering your settings

*SAVE & EXIT* (or *Start* on any row) writes your theme, range, tags, airport and relay into the
cartridge's own memory. Your Wi-Fi network is kept separately. Both stay when the power is off,
and even when the cartridge's program is updated.

There are *1,143* airports to choose from: every large airport in the world with scheduled
passenger service.
