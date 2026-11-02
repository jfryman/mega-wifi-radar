#import "lib.typ": *

= The Video Map

Faint grey lines on the scope show the runways around you, the way a controller's display draws
its *video map*. Here the scope is zoomed in to 10 nm over Nashville (KBNA).

#shot("map", callouts: ((1, 92, 112), (2, 148, 58), (3, 76, 176), (4, 30, 46)))
#v(1mm)
#key(1)[Runways][The short solid bars, drawn to scale: KBNA's longest is 11,000 ft, about
  1.8 nm, so even at 10 nm it is only 19 pixels long. Every listed airport in view gets its runways.]
#key(2)[Extended centrelines][The dashed lines: each runway's line carried straight on out
  past both ends. Arrivals fly down them to land. Only your home airport has them, and only out to 25 nm.]
#key(3)[Mile ticks][One every nautical mile from the runway's end (every 5 nm on the 25 nm scope).]
#key(4)[10 nm out][Where the centreline stops: about where an arrival joins the final approach.]

#pagebreak()
== Reading an approach

An airliner descends at about 3°, which is roughly *300 feet for every mile*. So the ticks double as
an altitude ruler:

#controls(
  ([10 NM], [about 3,000 ft: joining the approach]),
  ([5 NM], [about 1,600 ft]),
  ([1 NM], [about 300 ft: nearly on the runway]),
)

#v(2mm)
#align(center, box(width: 100%, height: 30mm, {
  // side view: the glidepath from 10 nm (left) down to the runway (right); 1 nm = w / 11
  let w = 100mm
  let x(nm) = w - (nm + 0.6) * w / 11
  let y(ft) = 24mm - ft / 3000 * 20mm
  place(top + left, line(start: (0mm, 24mm), end: (w, 24mm), stroke: 0.6pt + luma(160)))
  place(top + left, line(start: (x(0), 24mm), end: (w, 24mm), stroke: 2pt + ink.black))
  place(top + left, line(start: (x(10), y(3000)), end: (x(0), y(0)), stroke: (paint: ink.red, thickness: 1pt, dash: "dashed")))
  for nm in range(1, 11) {
    place(top + left, line(start: (x(nm), 22.5mm), end: (x(nm), 25.5mm), stroke: 0.6pt + ink.black))
  }
  for (nm, label) in ((10, [10 nm · 3,000 ft]), (5, [5 nm · 1,600 ft]), (1, [1 nm · 300 ft])) {
    place(top + left, dx: x(nm) - 1mm, dy: y(nm * 300) - 5mm, text(size: 6.5pt, label))
  }
  place(top + left, dx: x(0) - 7mm, dy: 26mm, text(size: 6.5pt, weight: "bold")[RUNWAY])
}))

An arrival lining up with a dashed line, its tag showing *030* and then lower, is heading for that
runway. A plane on a dashed line climbing away from the airport has just taken off. Which end
they use depends on the wind: they land and take off into it.

At 50 and 100 nm the centrelines would cover the whole scope, so only the runways are drawn,
and they shrink to a dot or two. *MAP* in Setup turns the video map off.

The runways come from OurAirports, and your relay draws the map for whichever airport you choose
as home.
