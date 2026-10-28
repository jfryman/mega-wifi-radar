// The front-of-box design, laid out for any trim size near 130 x 178 mm: used by the
// clamshell cover and (with a "manual" strip) the manual's front page. Positions scale with w.
#import "theme.typ": *
#import "keyart.typ": scope

#let front-art(w, h, strip: none) = {
  let k = w / 130mm                          // scale from the 130 mm reference layout
  let u(x) = x * k
  let b = bleed
  let at(x, y, body) = place(top + left, dx: b + x, dy: b + y, body)
  grid-fill(w + 2 * b, h + 2 * b)

  // top band
  place(top + left, rect(width: w + 2 * b, height: b + u(25mm), fill: ink.black, stroke: none))
  place(top + left, dy: b + u(25mm), rect(width: w + 2 * b, height: u(0.9mm), fill: ink.red, stroke: none))
  at(u(7mm), u(6mm), box(width: u(40mm), wordmark(size: 19pt * k)))
  at(u(7mm), u(17.4mm), banner(copy.system, size: 6.6pt * k))
  at(w - u(23mm), u(4.5mm), badge(d: u(16mm)))

  // key art window
  let (aw, ah) = (w - u(14mm), h - u(68mm))
  at(u(7mm), u(32mm), art-window(aw, ah, {
    place(top + left, rect(width: aw, height: ah,
      fill: gradient.linear(rgb("#000"), rgb("#04140b"), angle: 90deg)))
    let d = aw * 1.04
    let fx = (aw - d) / 2
    place(top + left, dx: fx, dy: u(2mm), scope(d, sweep: 58,
      view: (-fx / d + 0.01, 0, (aw - fx) / d - 0.01, 1)))
  }))

  // title over the bottom edge of the art
  at(u(9mm), u(32mm) + ah - u(18mm), title-lockup(size: 64pt * k))
  if strip != none { at(w - u(7mm) - u(46mm), u(32mm) + ah + u(9mm), box(width: u(46mm),
    align(right, banner(strip, size: 7.5pt * k)))) }

  at(u(7mm), h - u(24mm), text(..cond, weight: "semibold", style: "italic", size: 10pt * k,
    fill: ink.white, copy.tagline))

  // platform line
  at(u(7mm), h - u(14.5mm), block(width: w - u(14mm), {
    set text(..cond, weight: "bold", style: "italic", fill: ink.white, size: 6.4pt * k, tracking: 0.06em)
    copy.platform
    linebreak()
    text(fill: ink.silver, weight: "semibold", copy.requires)
  }))
}
