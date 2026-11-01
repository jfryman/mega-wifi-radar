#import "lib.typ": *

= Flyover Notices

When a plane passes right over your home, the radar can tell you. This needs a relay connected to a
flyover feed, a service that announces planes overhead (see the relay's documentation).

#align(center, shot("notice", width: 75%, callouts: ((1, 176, 20), (2, 200, 28), (3, 150, 36), (4, 116, 44))))
#v(1mm)
#key(1)[Headline][*OVERHEAD!*, *LOOK UP!*... and the callsign. *MAYDAY!*: an emergency.]
#key(2)[Airline][and the aircraft type.]
#key(3)[Route][altitude and ground speed, when known.]
#key(4)[Buttons][*C* opens the aircraft's details. *B* dismisses the notice; it goes by itself after
  15 seconds.]

A chime plays as it arrives (*CHIME* in Setup turns it off). Some home airports have a tune of their
own. In the Cockpit View, *OVERHEAD!* shows next to the callsign and the notice scrolls along the bottom.
