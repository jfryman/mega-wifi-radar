// Building blocks for manual pages: section headings, numbered steps, screenshots with
// callouts, key/value tables, notes. Sections import this and theme.typ.
#import "../theme.typ": *
#import "../hardware.typ": marker

// A screenshot from print/shots/, pixel-exact, black keyline; callouts at 320x224 pixel coords.
#let shot(name, width: 100%, callouts: ()) = layout(size => {
  let w = if type(width) == ratio { size.width * width } else { width }
  let s = w / 320
  box(width: w, height: 224 * s, stroke: 0.8pt + ink.black, {
    place(top + left, image("/print/shots/" + name + ".png", width: w, scaling: "pixelated"))
    for (n, x, y) in callouts { place(top + left, dx: x * s - 2.1mm, dy: y * s - 2.1mm, marker(n)) }
  })
})

// Numbered step: red disc number, bold lead-in, text.
#let step(n, lead, body) = grid(columns: (6mm, 1fr), column-gutter: 1.5mm, row-gutter: 0pt,
  marker(n, d: 4.6mm),
  block(above: 0pt, [#text(weight: "bold", lead) #body]))

// Callout legend entry (matches a marker on a drawing or shot).
#let key(n, lead, body) = grid(columns: (5.2mm, 1fr), column-gutter: 1.2mm,
  marker(n, d: 4mm), [#text(weight: "bold", lead) #body])

// Two-column control table.
#let controls(..rows) = {
  set par(justify: false)
  table(
  columns: (auto, 1fr), stroke: none, inset: (x: 1.6mm, y: 1.3mm),
  fill: (_, y) => if calc.odd(y) { rgb("#f1f1f3") } else { none },
  ..rows.pos().map(r => (text(..cond, weight: "bold", r.at(0)), r.at(1))).flatten(),
)}

// Boxed note, red rule on the left.
#let note(title, body) = block(width: 100%, inset: (left: 3mm, rest: 2mm), stroke: (left: 1.6pt + ink.red),
  fill: rgb("#f6f6f7"), [#text(..cond, weight: "bold", style: "italic", upper(title)) \ #body])
