// The instruction manual. Page 1 is the front design with an "INSTRUCTION MANUAL" strip, the
// last page is the back cover, and Notes pages pad the total to a multiple of 4 for saddle
// stitching. Sections live in print/manual/, one file per topic, in the order below.
#import "theme.typ": *
#import "front.typ": front-art
#import "keyart.typ": scope

#let (w, h) = size-of("manual")
#let sections = (
  "handling", "contents", "starting-up", "controls", "relay", "setup", "scope", "themes",
  "troubleshooting", "credits",
)

#let back-art(w, h) = {
  let b = bleed
  grid-fill(w + 2 * b, h + 2 * b)
  place(top + left, dx: b + (w - 60mm) / 2, dy: b + 22mm, scope(60mm, sweep: 300, tags: false, detail: 0.7))
  place(top + left, dx: b, dy: b + 92mm, block(width: w, align(center, {
    box(width: 40mm, wordmark(size: 17pt))
    v(3mm)
    text(..cond, weight: "bold", style: "italic", size: 8pt, fill: ink.white, tracking: 0.1em, copy.subtitle)
  })))
  place(top + left, dx: b + 10mm, dy: b + h - 32mm, block(width: w - 20mm, {
    set text(size: 6pt, fill: ink.silver)
    set par(justify: true)
    align(center, [#copy.url #sym.space.en · #sym.space.en #version])
    v(1mm)
    copy.disclaimer
  }))
}

#let art(n) = context {
  let last = counter(page).final().first()
  if n == 1 { front-art(w, h, strip: "INSTRUCTION MANUAL") }
  else if n == last { back-art(w, h) }
}

#show: printed.with(w, h, margin: (x: 11mm, top: 12mm, bottom: 15mm), art: art)
#set page(footer: context {
  let n = here().page()
  if n > 1 and n < counter(page).final().first() {
    align(center, box(fill: ink.black, inset: (x: 2.2mm, y: 1mm),
      text(..cond, weight: "bold", size: 8pt, fill: ink.white, str(n))))
  }
}, footer-descent: 40%)
#set par(justify: true, leading: 0.55em, spacing: 0.9em)
#set text(size: 8.8pt, hyphenate: true)

#show heading.where(level: 1): it => {
  pagebreak(weak: true)
  block(width: 100%, below: 4mm, stack(
    block(width: 100%, fill: ink.black, inset: (x: 3mm, y: 2.2mm), skew(ax: -10deg,
      text(..cond, weight: "black", style: "italic", size: 15pt, fill: ink.white, tracking: 0.02em, upper(it.body)))),
    block(width: 100%, height: 1mm, fill: ink.red),
  ))
}
#show heading.where(level: 2): it => block(above: 3.5mm, below: 1.8mm,
  text(..cond, weight: "bold", style: "italic", size: 10.5pt, fill: ink.red, upper(it.body)))

// page 1: the front design is the page art; nothing flows on it
#box()
#pagebreak()

#for s in sections { include "manual/" + s + ".typ" }

// Notes pages up to a multiple of 4 (counting the back cover), then the back cover.
// (A full last page pushes this to the top of an empty next page: don't count that one.)
#context {
  let p = here().position()
  let n = if p.y < bleed + slug + 20mm { p.page - 1 } else { p.page }
  let pad = calc.rem(4 - calc.rem(n + 1, 4), 4)
  for _ in range(pad) {
    pagebreak(weak: true)
    heading(outlined: false)[Notes]
    v(4mm)
    stack(spacing: 6.5mm, ..range(18).map(_ => line(length: 100%, stroke: 0.4pt + rgb("#b5b5ba"))))
  }
}
#pagebreak(weak: true)
#box()
