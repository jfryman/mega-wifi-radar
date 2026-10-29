#import "lib.typ": *

= Aircraft Details

Select an aircraft with *up* and *down*, then press *C*. The radar keeps running underneath;
*up* and *down* step through the list, and *C* or *B* closes the details.

#align(center, shot("popup", width: 86%, callouts: ((1, 215, 12), (2, 215, 36), (3, 215, 84), (4, 215, 136), (5, 215, 176))))
#v(1mm)
#key(1)[Callsign][and, on the right, the ICAO address: the transponder's permanent code.]
#key(2)[The aircraft][Registration, type code and year built; the model; the owner or operator.]
#key(3)[Altitude][Pressure altitude and vertical speed, the GPS altitude, the altitude and
  heading the crew has *selected* on the autopilot, and the altimeter setting in inches and hPa.]
#key(4)[Movement][Ground speed, track, the squawk code (*7500* hijack, *7600* radio failure, *7700*
  emergency, *1200* VFR), and the category (light, large, heavy, rotorcraft...).]
#key(5)[Position][Distance and bearing from your airport, airborne or on the ground, and how long
  ago it was last heard.]

It all updates each sweep. *--* is a value the aircraft isn't sending; *NO DETAILS FROM THE RELAY*
means only the live values are known.
