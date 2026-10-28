// Shared look for every printed piece: sizes, copy, colours, fonts, the print box
// (bleed + crop marks), the early-Genesis black grid, banner and title lockup.
// Design: lode/print/print-assets.md.

#let dims = toml("dims.toml")
#let copy = toml("copy.toml")
#let marks = sys.inputs.at("marks", default: "true") == "true"
#let version = sys.inputs.at("version", default: "dev")

#let bleed = dims.bleed * 1mm
#let slug = if marks { dims.slug * 1mm } else { 0mm }

#let ink = (
  black: rgb("#0b0b0c"),
  grid: rgb("#3b3b3f"),
  red: rgb("#d7141a"),
  red-dark: rgb("#8e0b10"),
  white: rgb("#ffffff"),
  silver: rgb("#c9ccd1"),
  phosphor: rgb("#46ff6a"),
  phosphor-dim: rgb("#1f7a33"),
  scope-bg: rgb("#03140a"),
  cyan: rgb("#5fe8ff"),
  amber: rgb("#ffcc33"),
  paper: rgb("#ffffff"),
  text: rgb("#111111"),
)

// Barlow Condensed is the Barlow family at 75% stretch: use `text(..cond, ...)`.
#let cond = (font: "Barlow", stretch: 75%)
#let font = (
  body: "Barlow",
  pixel: "Press Start 2P",
)

// ------------------------------------------------------------ print box

// Crop marks: short hairlines in the slug, aligned to the trim edges, never entering the bleed.
#let crop-marks(w, h) = {
  let gap = 1.5mm
  let len = slug - gap - 1mm
  let o = slug + bleed                       // trim corner offset from the page edge
  let st = 0.3pt + black
  for (x, y) in ((o, o), (o + w, o), (o, o + h), (o + w, o + h)) {
    let at-left = x == o
    let at-top = y == o
    // horizontal mark beside the corner, vertical mark above/below it
    place(top + left, dx: if at-left { x - bleed - gap - len } else { x + bleed + gap }, dy: y,
      line(length: len, stroke: st))
    place(top + left, dx: x, dy: if at-top { y - bleed - gap - len } else { y + bleed + gap },
      line(angle: 90deg, length: len, stroke: st))
  }
}

// Page set-up for a piece with trim size (w, h). `art` fills the bleed box behind the
// content (a function of the page number, or none). Content gets `margin` inside the trim.
#let printed(w, h, margin: 0mm, art: none, doc) = {
  set page(
    width: w + 2 * (bleed + slug),
    height: h + 2 * (bleed + slug),
    margin: if type(margin) == dictionary {
      margin.pairs().map(((k, v)) => (k, v + bleed + slug)).to-dict()
    } else { margin + bleed + slug },
    fill: white,
    background: context {
      if art != none {
        place(top + left, dx: slug, dy: slug,
          block(width: w + 2 * bleed, height: h + 2 * bleed, clip: true, art(here().page())))
      }
      if marks { crop-marks(w, h) }
    },
  )
  set text(font: font.body, size: 9pt, fill: ink.text)
  doc
}

// Trim sizes as lengths.
#let size-of(key) = (dims.at(key).width * 1mm, dims.at(key).height * 1mm)

// ------------------------------------------------------------ motifs

// The 1989 US black box: fine grey grid on black.
#let grid-tiling(cell: 4.6mm, weight: 0.35pt) = tiling(size: (cell, cell))[
  #place(rect(width: cell, height: cell, fill: ink.black, stroke: none))
  #place(line(length: cell, stroke: weight + ink.grid))
  #place(line(angle: 90deg, length: cell, stroke: weight + ink.grid))
]

#let grid-fill(w, h, cell: 4.6mm) = rect(width: w, height: h, fill: grid-tiling(cell: cell), stroke: none)

// A slanted red bar with white italic caps: the "16-BIT CARTRIDGE" strip.
#let banner(body, size: 7pt, fill: ink.red, pad: 1.6mm) = box(
  fill: fill, inset: (x: pad * 1.6, y: pad * 0.7),
  skew(ax: -12deg, text(..cond, weight: "bold", style: "italic", size: size,
    fill: ink.white, tracking: 0.08em, body)),
)

// Our own wordmark (not a Sega mark): "MEGA WIFI" in heavy italic with a red underline swoosh.
#let wordmark(size: 14pt) = box({
  set text(..cond, weight: "black", style: "italic", size: size, fill: ink.white, tracking: 0.02em)
  stack(spacing: size * 0.12,
    copy.pretitle,
    box(width: 100%, height: size * 0.14, fill: gradient.linear(ink.red, ink.red-dark)),
  )
})

// The big title: chrome-ish fill, red drop shadow, slanted like a 1989 logo.
#let title-lockup(size: 48pt, sub: true) = {
  let t = text(..cond, weight: "black", style: "italic", size: size, tracking: 0.01em,
    copy.title)
  let chrome = gradient.linear(ink.white, ink.silver, rgb("#7c8088"), ink.white, angle: 90deg)
  box(skew(ax: -8deg, stack(spacing: size * 0.08,
    box({
      place(dx: size * 0.05, dy: size * 0.05, text(fill: ink.red, t))
      text(fill: chrome, stroke: 0.6pt + ink.black, t)
    }),
    if sub {
      text(..cond, weight: "bold", style: "italic", size: size * 0.2, fill: ink.white,
        tracking: 0.14em, copy.subtitle)
    },
  )))
}

// Round red "LIVE ADS-B DATA" badge (ours; deliberately not seal-shaped).
#let badge(d: 16mm) = box(width: d, height: d, {
  place(circle(radius: d / 2, fill: ink.red, stroke: 0.8pt + ink.white))
  place(center + horizon, align(center, text(..cond, weight: "black", style: "italic",
    size: d * 0.22, fill: ink.white, {
      set par(leading: d * 0.05)
      copy.badge.join(linebreak())
    })))
})

// A white-framed window for key art, with a thin red inner keyline.
#let art-window(w, h, body) = box(width: w, height: h, stroke: 1.1pt + ink.white, clip: true, {
  place(top + left, body)
  place(top + left, dx: 1mm, dy: 1mm, rect(width: w - 2mm, height: h - 2mm, stroke: 0.4pt + ink.red))
})
