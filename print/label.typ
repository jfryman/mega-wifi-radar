// Cartridge front label: the cover in miniature. Wordmark band, framed key art, title.
#import "theme.typ": *
#import "keyart.typ": scope

#let (w, h) = size-of("label")

#let art(_) = {
  let b = bleed
  let at(x, y, body) = place(top + left, dx: b + x, dy: b + y, body)
  grid-fill(w + 2 * b, h + 2 * b, cell: 3.2mm)

  // top band
  place(top + left, rect(width: w + 2 * b, height: b + 13mm, fill: ink.black, stroke: none))
  place(top + left, dy: b + 13mm, rect(width: w + 2 * b, height: 0.6mm, fill: ink.red, stroke: none))
  at(4.5mm, 3.2mm, box(width: 26mm, wordmark(size: 12pt)))
  at(4.5mm, 9.4mm, banner(copy.system, size: 4.4pt, pad: 1.1mm))
  at(w - 4.5mm - 10.5mm, 1.3mm, badge(d: 10.5mm))

  // key art
  let (ax, ay, aw, ah) = (4.5mm, 16.5mm, w - 9mm, 47mm)
  at(ax, ay, art-window(aw, ah, {
    let d = aw * 1.05
    let dy = -d * 0.12
    let fx = (aw - d) / 2
    place(top + left, dx: fx, dy: dy, scope(d, sweep: 58, tag-size: 0.02, detail: 0.6,
      view: (-fx / d + 0.01, -dy / d + 0.01, (aw - fx) / d - 0.01, (ah - dy) / d - 0.12)))
  }))

  // title + platform line
  at(5.5mm, 56.5mm, title-lockup(size: 36pt))
  at(4.5mm, h - 4.6mm, text(..cond, weight: "bold", style: "italic", size: 3.9pt, fill: ink.silver,
    tracking: 0.05em, copy.platform))
}

#show: printed.with(w, h, art: art)
