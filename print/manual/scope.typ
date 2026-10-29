#import "lib.typ": *

= Reading the Scope

#shot("radar", callouts: ((1, 160, 62), (2, 207, 37), (3, 199, 121), (4, 18, 76), (5, 215, 28), (6, 215, 110), (7, 215, 198)))
#v(1mm)
#key(1)[The sweep][turns once every 4.3 seconds, leaving a fading glow behind it. Each aircraft
  moves to its new position as the sweep passes over it, and dims until the next pass.]
#key(2)[Range rings][Five rings, each labelled with its distance in nautical miles.]
#key(3)[Aircraft][A bright square is a fresh report. The fading dots behind it are its last five
  positions, about 20 seconds of history.]
#key(4)[Data tag][Callsign on top. Underneath: altitude in hundreds of feet, then ground speed in
  knots. *330 400* is 33,000 ft at 400 knots. Aircraft on the ground get no tag.]

#pagebreak()
== The side panel

#key(5)[Status][Your airport, the range (*RNG*), the time in UTC (*Z*), the number of aircraft, and
  the link: *NET OK*, or *STALE* with the seconds since the last good update.]
#key(6)[Traffic list][Every aircraft on the scope: callsign and distance in nautical miles. The
  top line shows the order (*NEAREST*, *A-Z*, *Z-A*, *LOW ALT*, *HIGH ALT*; hold *C* to change it).
  The selected aircraft is highlighted and boxed on the scope. Emergencies are always at the top,
  in red. *v13 MORE* means there are more below: keep pressing *down*.]
#key(7)[Theme, tags and data][The theme, the tag setting (*ALL*, *SEL*, *OFF*), and where the
  traffic comes from.]

== Tag colours

#controls(
  (text(fill: ink.phosphor-dim)[GREEN], [Below 10,000 ft, or on the ground]),
  (text(fill: rgb("#1aa5c4"))[CYAN], [10,000 to 17,999 ft]),
  ([WHITE], [18,000 ft and above (flight levels)]),
  (text(fill: ink.red)[RED], [Emergency: squawk 7500, 7600 or 7700]),
)

== Old reports

An aircraft not heard from for 15 seconds is drawn dim, and one silent for a minute disappears.
If the whole feed stops, the panel shows *STALE* and the time since the last update. The radar
keeps sweeping what it has.
