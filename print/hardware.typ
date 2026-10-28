// Line drawings of the hardware for the manual, in Typst vectors (our own drawings, no logos):
// Genesis Model 1 (top and front), the 3-button Control Pad, and the cartridge. Each takes a
// width and an optional list of numbered callouts at points in the drawing's own units.
#import "theme.typ": ink, font, cond

#let body-fill = rgb("#26262a")
#let edge = 0.7pt + black
#let detail = rgb("#4a4a50")
#let gold = rgb("#c9a227")

// A red numbered disc, as used in the manual's callouts.
#let marker(n, d: 4.2mm) = box(width: d, height: d, {
  place(circle(radius: d / 2, fill: ink.red, stroke: 0.6pt + white))
  place(center + horizon, text(..cond, weight: "black", size: d * 0.7, fill: white, str(n)))
})

// Draw `body` in a (uw x uh)-unit canvas scaled to width w; callouts: ((n, x, y), ...).
#let canvas(w, uw, uh, callouts, body) = {
  let s = w / uw
  box(width: w, height: uh * s, {
    body(s)
    for (n, x, y) in callouts {
      place(top + left, dx: x * s - 2.1mm, dy: y * s - 2.1mm, marker(n))
    }
  })
}

#let rr(s, x, y, w, h, r: 0, ..args) = place(top + left, dx: x * s, dy: y * s,
  rect(width: w * s, height: h * s, radius: r * s, ..args))
#let circ(s, x, y, r, ..args) = place(top + left, dx: (x - r) * s, dy: (y - r) * s,
  circle(radius: r * s, ..args))
#let label(s, x, y, size, body, fill: white) = place(top + left, dx: x * s, dy: y * s,
  text(..cond, weight: "bold", size: size * s, fill: fill, tracking: 0.05em, top-edge: "cap-height", bottom-edge: "baseline", body))

// Model 1, seen from above (280 x 212 units, front edge at the bottom).
#let model1-top(w, callouts: ()) = canvas(w, 280, 212, callouts, s => {
  rr(s, 0, 0, 280, 212, r: 12, fill: body-fill, stroke: edge)
  rr(s, 6, 6, 268, 18, r: 6, fill: rgb("#1d1d20"), stroke: 0.4pt + detail)          // rear step
  for i in range(0, 9) { rr(s, 196 + i * 8, 34, 3, 26, r: 1.5, fill: rgb("#151517")) }  // vents
  circ(s, 140, 104, 84, fill: rgb("#2e2e33"), stroke: 0.6pt + detail)                 // dome
  circ(s, 140, 104, 72, fill: none, stroke: 0.5pt + gold)
  label(s, 124, 176, 10, "16-BIT", fill: gold)
  rr(s, 78, 62, 124, 20, r: 3, fill: rgb("#0c0c0d"), stroke: 0.5pt + detail)         // cartridge slot
  rr(s, 82, 70, 116, 4, r: 1, fill: rgb("#3a3a40"))                                   // dust flaps
  // power slide (front left) and reset (front right)
  label(s, 22, 160, 8, "POWER")
  rr(s, 22, 170, 40, 14, r: 3, fill: rgb("#111113"), stroke: 0.4pt + detail)
  rr(s, 24, 171, 16, 12, r: 2, fill: rgb("#8a8a92"))
  label(s, 234, 160, 8, "RESET")
  rr(s, 234, 170, 30, 14, r: 4, fill: rgb("#8a8a92"), stroke: 0.4pt + black)
  // volume slider + headphone jack (front left corner)
  label(s, 20, 114, 7, "VOLUME")
  rr(s, 20, 124, 36, 6, r: 3, fill: rgb("#111113"))
  rr(s, 30, 121, 7, 12, r: 2, fill: rgb("#8a8a92"))
  circ(s, 70, 127, 5, fill: rgb("#111113"), stroke: 0.4pt + detail)
})

// Model 1 front face (280 x 46 units): the two control ports.
#let model1-front(w, callouts: ()) = canvas(w, 280, 46, callouts, s => {
  rr(s, 0, 0, 280, 46, r: 8, fill: body-fill, stroke: edge)
  for (i, x) in ((1, 86), (2, 140)) {
    // D-shaped 9-pin port
    rr(s, x, 14, 40, 18, r: 6, fill: rgb("#0e0e10"), stroke: 0.5pt + detail)
    for p in range(0, 5) { circ(s, x + 8 + p * 6, 20, 1.3, fill: rgb("#9a9aa2")) }
    for p in range(0, 4) { circ(s, x + 11 + p * 6, 26, 1.3, fill: rgb("#9a9aa2")) }
    label(s, x + 4, 36, 7, "CONTROL " + str(i))
  }
})

// The 3-button Control Pad (180 x 112 units, cord at the top).
#let pad(w, callouts: ()) = canvas(w, 180, 112, callouts, s => {
  place(top + left, dx: 90 * s, dy: 0pt, line(angle: 90deg, length: 22 * s, stroke: (4 * s) + body-fill))
  rr(s, 5, 20, 170, 88, r: 42, fill: body-fill, stroke: edge)
  circ(s, 90, 6, 18, fill: white, stroke: none)                    // the notch at the top
  place(top + left, dx: 90 * s, dy: 0pt, line(angle: 90deg, length: 22 * s, stroke: (4 * s) + body-fill))
  // D-button
  circ(s, 46, 64, 26, fill: rgb("#1a1a1d"), stroke: 0.5pt + detail)
  rr(s, 39, 42, 14, 44, r: 2, fill: rgb("#3c3c43"))
  rr(s, 24, 57, 44, 14, r: 2, fill: rgb("#3c3c43"))
  circ(s, 46, 64, 4, fill: rgb("#26262a"))
  // Start
  rr(s, 80, 70, 22, 8, r: 4, fill: rgb("#55555c"))
  label(s, 80, 82, 7, "START")
  // A, B, C
  for (i, (x, y)) in ((122, 78), (139, 66), (156, 54)).enumerate() {
    circ(s, x, y, 9.5, fill: rgb("#3c3c43"), stroke: 0.5pt + black)
    place(top + left, dx: (x - 3.4) * s, dy: (y + 10.5) * s,
      text(..cond, weight: "black", size: 10 * s, fill: rgb("#b8b8c0"), top-edge: "cap-height", bottom-edge: "baseline", "ABC".at(i)))
  }
})

// The cartridge, label towards you, with an arrow into the slot (100 x 120 units).
#let cartridge(w, art: none, callouts: ()) = canvas(w, 100, 120, callouts, s => {
  rr(s, 8, 8, 84, 96, r: 4, fill: body-fill, stroke: edge)
  rr(s, 16, 4, 68, 10, r: 2, fill: body-fill, stroke: edge)
  rr(s, 15, 20, 70, 64, r: 2, fill: ink.black, stroke: 0.4pt + detail)
  if art != none { place(top + left, dx: 15 * s, dy: 20 * s, box(width: 70 * s, height: 64 * s, clip: true, art(70 * s, 64 * s))) }
  for i in range(0, 6) { rr(s, 20 + i * 11, 90, 6, 10, r: 1, fill: rgb("#1a1a1d")) }  // grip ridges
  // arrow down
  place(top + left, dx: 50 * s, dy: 104 * s, line(angle: 90deg, length: 10 * s, stroke: 1.6pt + ink.red))
  place(top + left, dx: 50 * s - 4 * s, dy: 112 * s,
    polygon(fill: ink.red, (0pt, 0pt), (8 * s, 0pt), (4 * s, 7 * s)))
})
