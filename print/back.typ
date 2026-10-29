// The back of the clamshell: header lockup, pitch, feature bullets, "what you need", three framed
// screenshots with red caption banners, and the platform / URL / disclaimer lines. Copy: copy.toml [back].
// Laid out on the 130 mm reference like front.typ; positions scale with w.
#import "theme.typ": *

// A screenshot in a white frame, its caption on a red banner over the bottom-left corner.
#let framed-shot(name, w, caption, k) = box(width: w, height: w * 224 / 320 + 3mm * k, {
  place(top + left, box(width: w, height: w * 224 / 320, stroke: 1.1pt + ink.white, clip: true,
    image("/print/shots/" + name + ".png", width: w, scaling: "pixelated")))
  place(top + left, dx: -1.5mm * k, dy: w * 224 / 320 - 2.2mm * k, banner(caption, size: 6pt * k))
})

#let back-art(w, ht) = {
  let k = w / 130mm
  let u(x) = x * k
  let b = bleed
  let at(x, y, body) = place(top + left, dx: b + x, dy: b + y, body)
  let c = copy.back
  grid-fill(w + 2 * b, ht + 2 * b)

  // header band, as on the front
  place(top + left, rect(width: w + 2 * b, height: b + u(25mm), fill: ink.black, stroke: none))
  place(top + left, dy: b + u(25mm), rect(width: w + 2 * b, height: u(0.9mm), fill: ink.red, stroke: none))
  at(u(7mm), u(4.5mm), box(width: u(30mm), wordmark(size: 13pt * k)))
  at(u(7mm), u(11mm), title-lockup(size: 30pt * k, sub: false))
  at(w - u(7mm) - u(56mm), u(9mm), box(width: u(56mm), align(right, {
    banner(copy.system, size: 6.2pt * k)
    v(u(1.5mm))
    text(..cond, weight: "bold", style: "italic", size: 7pt * k, fill: ink.silver, tracking: 0.1em,
      copy.subtitle)
  })))

  // pitch, full width
  at(u(7mm), u(30mm), block(width: w - u(14mm), {
    set par(justify: false, leading: 0.45em)
    text(..cond, weight: "semibold", style: "italic", size: 10pt * k, fill: ink.white, c.pitch)
  }))

  // left column: features, then what you need
  let lw = u(60mm)
  at(u(7mm), u(48mm), block(width: lw, {
    set par(justify: false, leading: 0.4em)
    {
      set text(..cond, size: 8.4pt * k, fill: ink.white, weight: "medium")
      for f in c.features {
        grid(columns: (u(3.6mm), 1fr), row-gutter: 0pt,
          text(fill: ink.red, weight: "black", size: 9pt * k, "▶"), f)
        v(u(2mm), weak: true)
      }
    }
    v(u(5mm))
    block(width: lw, stroke: 0.8pt + ink.white, inset: u(2.4mm), fill: ink.black, {
      set par(leading: 0.35em)
      text(..cond, weight: "black", style: "italic", size: 8pt * k, fill: ink.red, tracking: 0.08em, c.need-title)
      v(u(1.4mm), weak: true)
      set text(..cond, size: 7.4pt * k, fill: ink.white)
      for n in c.need { [• #n]; linebreak() }
    })
  }))

  // right column: three shots, staggered inwards, captions on their bottom-left corners
  let sw = u(48mm)
  let sx = w - u(7mm) - sw
  for (i, name) in c.shots.enumerate() {
    let dx = if calc.odd(i) { -u(4mm) } else { 0mm }
    at(sx + dx, u(46mm) + i * u(38mm), framed-shot(name, sw, c.captions.at(i), k))
  }

  // platform, url, version, disclaimer
  at(u(7mm), ht - u(18.5mm), block(width: w - u(14mm), {
    set par(justify: false, leading: 0.4em)
    set text(..cond, style: "italic", fill: ink.white, tracking: 0.05em)
    text(weight: "bold", size: 6.4pt * k, copy.platform)
    linebreak()
    text(weight: "semibold", size: 6.4pt * k, fill: ink.silver, copy.requires)
    v(u(1.6mm), weak: true)
    text(style: "normal", size: 6.4pt * k, weight: "bold", copy.url)
    h(u(3mm))
    text(style: "normal", size: 6pt * k, fill: ink.silver, version)
    v(u(1.2mm), weak: true)
    text(style: "normal", size: 5.2pt * k, fill: ink.silver, tracking: 0em, copy.disclaimer)
  }))
}

// The spine: black, red rules at both edges, the title running top to bottom.
#let spine-art(sp, ht) = {
  let b = bleed
  let k = sp / 26mm
  rect(width: sp, height: ht + 2 * b, fill: ink.black, stroke: none, {
    place(top + left, rect(width: 0.8mm, height: ht + 2 * b, fill: ink.red, stroke: none))
    place(top + right, rect(width: 0.8mm, height: ht + 2 * b, fill: ink.red, stroke: none))
    place(center + horizon, rotate(90deg, reflow: true, box(height: sp, align(horizon, stack(dir: ltr,
      spacing: 5mm * k,
      box(width: 30mm * k, wordmark(size: 15pt * k)),
      title-lockup(size: 34pt * k, sub: false),
      h(16mm * k),
      banner(copy.system, size: 6.4pt * k),
    )))))
  })
}
