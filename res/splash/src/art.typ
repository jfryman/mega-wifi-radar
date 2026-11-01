// Boot splash art, drawn as vectors and rendered at 72 ppi (1 pt = 1 px) by scripts/splash-art,
// then quantized to the Genesis palettes in palettes.json by tools/splashgen.
// Pages: 1 scene (320x224), 2 wordmark (288x48), 3-6 the 787 at 0, 6, 12, 18 degrees (96x48),
// 7 speech-bubble tiles (64x32).
// Design: lode/ui/boot-splash.md.

#let pal = json("palettes.json")
#let c(name, i) = {
  let (r, g, b) = pal.at(name).colors.at(i)
  rgb(int(calc.round(r * 255 / 7)), int(calc.round(g * 255 / 7)), int(calc.round(b * 255 / 7)))
}
#let bg(i) = c("bg", i)
#let pl(i) = c("plane", i)

#set text(font: "Barlow")

// ------------------------------------------------------------ 1: scene
// The ground sits at the bottom edge: the wordmark is across the top and the sky between is
// where the speech bubbles open (lode/ui/boot-splash.md).
#let sky-h = 182
#let g = 32   // everything on the ground is drawn 32 px lower than the first layout
#page(width: 320pt, height: 224pt, margin: 0pt, fill: bg(0))[
  // sky: smooth gradient, dithered by splashgen
  #place(top + left, rect(width: 320pt, height: sky-h * 1pt, fill: gradient.linear(
    (bg(1), 0%), (bg(2), 25%), (bg(3), 45%), (bg(4), 62%), (bg(5), 80%), (bg(6), 92%), (bg(7), 100%),
    angle: 90deg)))
  // distant hills
  #place(top + left, dy: (sky-h - 8) * 1pt, polygon(fill: bg(13),
    (0pt, 8pt), (40pt, 2pt), (90pt, 6pt), (150pt, 0pt), (210pt, 5pt), (270pt, 1pt), (320pt, 6pt), (320pt, 8pt)))
  // ground
  #place(top + left, dy: sky-h * 1pt, rect(width: 320pt, height: (224 - sky-h) * 1pt, fill: bg(9)))
  #place(top + left, dy: 216pt, rect(width: 320pt, height: 8pt, fill: bg(8)))
  // runway: edges, centre-line dashes, red edge lights
  #place(top + left, dy: (162 + g) * 1pt, rect(width: 320pt, height: 20pt, fill: bg(10)))
  #place(top + left, dy: (162 + g) * 1pt, rect(width: 320pt, height: 1pt, fill: bg(15)))
  #place(top + left, dy: (181 + g) * 1pt, rect(width: 320pt, height: 1pt, fill: bg(15)))
  #for x in range(4, 320, step: 24) { place(top + left, dx: x * 1pt, dy: (171.5 + g) * 1pt, rect(width: 12pt, height: 1pt, fill: bg(15))) }
  #for x in range(10, 320, step: 32) {
    place(top + left, dx: x * 1pt, dy: (159 + g) * 1pt, rect(width: 2pt, height: 2pt, fill: bg(11)))
    place(top + left, dx: x * 1pt, dy: (183 + g) * 1pt, rect(width: 2pt, height: 2pt, fill: bg(11)))
  }
  // hold-short bars at x = 150 (the 787 stops with its nose just before them)
  #place(top + left, dx: 150pt, dy: (162 + g) * 1pt, rect(width: 1pt, height: 20pt, fill: bg(14)))
  #place(top + left, dx: 153pt, dy: (162 + g) * 1pt, rect(width: 1pt, height: 20pt, fill: bg(14)))

  // control tower (left): shaft, cab, windows, roof, antenna + beacon
  #let tx = 14
  #let at(x, y, body) = place(top + left, dx: x * 1pt, dy: (y + g) * 1pt, body)
  #at(tx + 10, 92, rect(width: 18pt, height: 72pt, fill: bg(12)))
  #at(tx + 22, 92, rect(width: 6pt, height: 72pt, fill: bg(13)))
  #for y in range(100, 160, step: 12) { at(tx + 14, y, rect(width: 3pt, height: 4pt, fill: bg(14))) }
  #at(tx + 4, 86, rect(width: 30pt, height: 6pt, fill: bg(13)))
  #at(tx, 70, polygon(fill: bg(12), (0pt, 0pt), (38pt, 0pt), (34pt, 16pt), (4pt, 16pt)))
  #for i in range(0, 5) { at(tx + 4 + i * 6.5, 73, rect(width: 4.5pt, height: 8pt, fill: bg(14))) }
  #at(tx - 2, 66, rect(width: 42pt, height: 4pt, fill: bg(13)))
  #at(tx + 18, 52, rect(width: 1.5pt, height: 14pt, fill: bg(13)))
  #at(tx + 17, 49, rect(width: 4pt, height: 3pt, fill: bg(11)))
  // apron by the tower
  #at(0, 164, rect(width: 70pt, height: 3pt, fill: bg(10)))
]

// ------------------------------------------------------------ 2: wordmark
// Three layers: a wide navy outline, a white rim, the blue body (splashgen re-indexes the body
// into the diagonal shine slots).
#page(width: 288pt, height: 48pt, margin: 0pt, fill: none)[
  #let word(..args) = text(font: "Barlow", stretch: 75%, weight: "black", style: "italic", size: 40pt,
    tracking: 0.5pt, ..args, box("MEGA WIFI RADAR"))
  #let at(body) = place(center + horizon, dy: 1pt, body)
  #at(word(fill: c("logo", 1), stroke: (paint: c("logo", 1), thickness: 6pt, join: "round")))
  #at(word(fill: c("logo", 2), stroke: (paint: c("logo", 2), thickness: 3pt, join: "round")))
  #at(word(fill: c("logo", 3)))
]

// ------------------------------------------------------------ 3-6: the 787
// Side view facing right, drawn in a 100x80 box whose centre (50, 40) is the main-gear contact
// point, so rotate() pitches it about the wheels.
#let plane(gear: true) = box(width: 100pt, height: 80pt, {
  let p(x, y) = (x * 1pt, y * 1pt)
  let at(x, y, body) = place(top + left, dx: x * 1pt, dy: y * 1pt, body)
  // far wing (raked tip just visible above the fuselage) and tail plane
  at(0, 0, polygon(fill: pl(4), p(50, 25), p(40, 25), p(30, 14), p(33, 14)))
  at(0, 0, polygon(fill: pl(4), p(20, 24), p(9, 24), p(4, 20), p(7, 20)))
  // vertical tail: navy with our red stripe
  at(0, 0, polygon(fill: pl(5), p(22, 22), p(8, 22), p(3, 4), p(9, 4)))
  at(0, 0, polygon(fill: pl(7), p(16, 22), p(13, 22), p(6, 8), p(8, 8)))
  // fuselage: tail cone, tube, smooth 787 nose
  at(0, 0, curve(fill: pl(2), stroke: 0.6pt + pl(1),
    curve.move(p(4, 21)),
    curve.line(p(80, 19.5)),
    curve.cubic(p(88, 19.5), p(94, 23), p(95, 26.5)),
    curve.cubic(p(94, 28.5), p(90, 29), p(84, 29)),
    curve.line(p(22, 29)),
    curve.cubic(p(14, 29), p(8, 25), p(4, 21)),
    curve.close()))
  // belly shading + red cheat line
  at(0, 0, curve(fill: pl(3), curve.move(p(22, 26.5)), curve.line(p(88, 26.5)),
    curve.cubic(p(90, 27), p(91, 28), p(84, 28.6)), curve.line(p(22, 28.6)), curve.close()))
  at(0, 0, polygon(fill: pl(7), p(24, 25.2), p(88, 25.2), p(88.5, 26), p(24, 26)))
  // cabin windows + cockpit
  for x in range(26, 80, step: 3) { at(x, 22, rect(width: 1pt, height: 1.2pt, fill: pl(8))) }
  at(0, 0, polygon(fill: pl(8), p(86.5, 21.6), p(90, 22.6), p(90.6, 23.8), p(86.5, 23.6)))
  // near wing: thin swept sliver from the wing box
  at(0, 0, polygon(fill: pl(9), stroke: 0.4pt + pl(1), p(56, 27.5), p(42, 28), p(24, 31.5), p(28, 31.8), p(46, 29.8)))
  // engine nacelle with chevrons on the trailing edge
  at(0, 0, curve(fill: pl(9), stroke: 0.5pt + pl(1),
    curve.move(p(48, 29)), curve.line(p(62, 29)),
    curve.cubic(p(64.5, 29), p(64.5, 34.5), p(62, 34.5)),
    curve.line(p(48, 34.5)), curve.close()))
  at(60.5, 29.6, rect(width: 1.6pt, height: 4.3pt, fill: pl(12)))
  for i in range(0, 3) { at(0, 0, polygon(fill: pl(12), p(48, 29.4 + i * 1.8), p(46.4, 30.3 + i * 1.8), p(48, 31.2 + i * 1.8))) }
  // landing gear
  if gear {
    at(80.8, 29, rect(width: 1pt, height: 8.4pt, fill: pl(10)))
    at(79.6, 37, circle(radius: 1.5pt, fill: pl(11)))
    at(49.4, 29, rect(width: 1.4pt, height: 8.2pt, fill: pl(10)))
    at(46.8, 37, circle(radius: 1.6pt, fill: pl(11)))
    at(50.4, 37, circle(radius: 1.6pt, fill: pl(11)))
  }
})

#for (deg, gear) in ((0, true), (6, true), (12, true), (18, false)) {
  page(width: 96pt, height: 48pt, margin: 0pt, fill: none,
    place(top + left, dx: -2pt, dy: 7pt, rotate(-deg * 1deg, origin: center + horizon, plane(gear: gear))))
}

// ------------------------------------------------------------ 7: speech-bubble tiles
// 8x4 tiles; splashgen takes 13 of them at fixed spots (see bubbleTiles there). Every shape is
// drawn twice: fat in the outline colour, then plain in white, so overlapping shapes merge into
// one outline. Shapes sit 1.5 px inside the tile edges, so the outline lands in the outer pixels.
#let ink = c("text", 1)
#let paper = c("text", 2)
#let bubble-shapes(fill, stroke) = {
  let s = if stroke { 3pt + ink } else { none }
  let at(x, y, body) = place(top + left, dx: x * 1pt, dy: y * 1pt, body)
  // A: plain 3x3 bubble -> corners, edges, fill
  at(1.5, 1.5, rect(width: 21pt, height: 21pt, radius: 5pt, fill: fill, stroke: s))
  // B: 4x3 bubble at tiles 4..7 with a tail out of its left edge (tile 3, row 1) and one
  // down out of its bottom edge (tile 6, row 3)
  at(33.5, 1.5, rect(width: 29pt, height: 21pt, radius: 5pt, fill: fill, stroke: s))
  at(0, 0, polygon(fill: fill, stroke: s, (34pt, 9pt), (25.5pt, 14.5pt), (34pt, 14pt)))
  at(0, 0, polygon(fill: fill, stroke: s, (50pt, 22pt), (51pt, 30.5pt), (55pt, 22pt)))
}
#page(width: 64pt, height: 32pt, margin: 0pt, fill: none)[
  #bubble-shapes(ink, true)
  #bubble-shapes(paper, false)
]
