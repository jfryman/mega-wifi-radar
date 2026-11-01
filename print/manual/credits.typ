#import "lib.typ": *

= Credits

#grid(columns: (32mm, 1fr), row-gutter: 2.2mm, column-gutter: 3mm,
  text(..cond, weight: "bold")[PROGRAM & DESIGN], [James Fryman],
  text(..cond, weight: "bold")[CARTRIDGE], [MegaWiFi by doragasu, Rev.C board by James Fryman],
  text(..cond, weight: "bold")[DEVELOPMENT KIT], [SGDK by Stephane Dallongeville],
  ..if is32x { (text(..cond, weight: "bold")[32X START-UP], [after Chilly Willy's and marsdev's (andwn)]) } else { () },
  text(..cond, weight: "bold")[TRAFFIC DATA], [adsb.fi open data, from volunteer ADS-B receivers],
  text(..cond, weight: "bold")[AIRPORTS], [OurAirports (public domain)],
  text(..cond, weight: "bold")[ELEVATION], [AWS Terrain Tiles (Mapzen / Tilezen), from the sources below],
  text(..cond, weight: "bold")[TYPE], [Barlow and Press Start 2P (SIL Open Font License)],
)

#v(3mm)
#block(width: 100%, {
  set text(size: 5.6pt, fill: rgb("#444"))
  set par(leading: 0.42em, justify: false)
  [*Elevation data:* ArcticDEM terrain data DEM(s) were created from DigitalGlobe, Inc., imagery and
  funded under National Science Foundation awards 1043681, 1559691, and 1542736; Australia terrain data
  © Commonwealth of Australia (Geoscience Australia) 2017; Austria terrain data © offene Daten Österreichs
  – Digitales Geländemodell (DGM) Österreich; Canada terrain data contains information licensed under the
  Open Government Licence – Canada; Europe terrain data produced using Copernicus data and information
  funded by the European Union - EU-DEM layers; Global ETOPO1 terrain data U.S. National Oceanic and
  Atmospheric Administration; Mexico terrain data source: INEGI, Continental relief, 2016; New Zealand
  terrain data Copyright 2011 Crown copyright (c) Land Information New Zealand and the New Zealand
  Government (All rights reserved); Norway terrain data © Kartverket; United Kingdom terrain data
  © Environment Agency copyright and/or database right 2015. All rights reserved; United States 3DEP
  (formerly NED) and global GMTED2010 and SRTM terrain data courtesy of the U.S. Geological Survey.
  Resampled and coloured by the relay.]
})

#v(3mm)
#block(width: 100%, inset: 3mm, fill: rgb("#f1f1f3"), {
  set text(size: 8pt)
  [Software version *#version*. Source code, updates and this manual are free at
  *#copy.url*, under the MIT license.]
})

#v(1fr)
#text(size: 6.5pt, fill: rgb("#555"), copy.disclaimer)
