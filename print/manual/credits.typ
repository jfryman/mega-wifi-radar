#import "lib.typ": *

= Credits

#grid(columns: (32mm, 1fr), row-gutter: 2.2mm, column-gutter: 3mm,
  text(..cond, weight: "bold")[PROGRAM & DESIGN], [James Fryman],
  text(..cond, weight: "bold")[CARTRIDGE], [MegaWiFi by doragasu, Rev.C board by James Fryman],
  text(..cond, weight: "bold")[DEVELOPMENT KIT], [SGDK by Stephane Dallongeville],
  text(..cond, weight: "bold")[TRAFFIC DATA], [adsb.fi open data, from volunteer ADS-B receivers],
  text(..cond, weight: "bold")[AIRPORTS], [OurAirports (public domain)],
  text(..cond, weight: "bold")[TYPE], [Barlow and Press Start 2P (SIL Open Font License)],
)

#v(4mm)
#block(width: 100%, inset: 3mm, fill: rgb("#f1f1f3"), {
  set text(size: 8pt)
  [Software version *#version*. Source code, updates and this manual are free at
  *#copy.url*, under the MIT license.]
})

#v(1fr)
#text(size: 6.5pt, fill: rgb("#555"), copy.disclaimer)
