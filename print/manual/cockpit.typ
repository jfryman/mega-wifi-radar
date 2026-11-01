#import "lib.typ": *

= Cockpit View

Ride along with any aircraft on the radar. Open its details with *C*, then press *A*. On a six-button
controller, *Y* goes straight there for the selected aircraft. *B* takes you back to the radar.

#align(center, shot("cockpit", width: 70%, callouts: if is32x {
  ((1, 300, 40), (2, 140, 172), (3, 300, 188), (4, 300, 198), (5, 300, 220))
} else { ((1, 300, 50), (2, 300, 140), (3, 300, 156), (4, 300, 172), (5, 300, 204)) }))
#v(1mm)
#key(1)[The view][Straight ahead over the real ground, drawn by your #copy.machine as the aircraft flies.
  Turns tilt the horizon.]
#key(2)[Callsign][and the signal: *LIVE*, *NO SIGNAL* (no news for a moment) or *TERRAIN...*
  (loading the ground).]
#key(3)[Altitude][and ground speed.]
#key(4)[Movement][Vertical speed, heading, and the bank: *L* or *R* and degrees.]
#key(5)[Credit][Where the ground comes from (see _Credits_).]

The relay sends the ground and the aircraft's position every two seconds; your #copy.machine flies smoothly
in between, fetching more ground ahead. Getting in takes a few seconds.

#note[What you're seeing][Hills are true to scale, so flat country looks flat. Colours come from height,
not photographs, so rivers and towns don't show.]
