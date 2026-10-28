// Saddle-stitch imposition of out/print/manual-trim.pdf onto landscape Letter or A4:
// two trim-size pages per side at 100%, with crop marks and a fold mark. Print duplex,
// "flip on short edge", stack the sheets in order, fold, staple. --input paper=us-letter|a4
// and --input pages=N (scripts/print passes both).
#import "theme.typ": dims, bleed, cond

#let paper = sys.inputs.at("paper", default: "us-letter")
#let n = int(sys.inputs.pages)
#let (tw, th) = (dims.manual.width * 1mm, dims.manual.height * 1mm)
#let src = "/out/print/manual-trim.pdf"
#assert(calc.rem(n, 4) == 0, message: "manual page count must be a multiple of 4")

#set page(paper: paper, flipped: true, margin: 0mm)
#set text(font: "Barlow", size: 6pt, fill: rgb("#888"))

#let trim-page(p) = box(width: tw, height: th, clip: true,
  place(top + left, dx: -bleed, dy: -bleed, image(src, page: p, width: tw + 2 * bleed)))

#let spread(l, r, caption) = layout(size => {
  let x0 = (size.width - 2 * tw) / 2
  let y0 = (size.height - th) / 2
  place(top + left, dx: x0, dy: y0, trim-page(l))
  place(top + left, dx: x0 + tw, dy: y0, trim-page(r))
  let st = 0.3pt + black
  for x in (x0, x0 + 2 * tw) {
    place(top + left, dx: x, dy: y0 - 8mm, line(angle: 90deg, length: 6mm, stroke: st))
    place(top + left, dx: x, dy: y0 + th + 2mm, line(angle: 90deg, length: 6mm, stroke: st))
  }
  for y in (y0, y0 + th) {
    place(top + left, dx: x0 - 8mm, dy: y, line(length: 6mm, stroke: st))
    place(top + left, dx: x0 + 2 * tw + 2mm, dy: y, line(length: 6mm, stroke: st))
  }
  // fold
  let fold = (paint: black, thickness: 0.3pt, dash: "dashed")
  place(top + left, dx: x0 + tw, dy: y0 - 8mm, line(angle: 90deg, length: 6mm, stroke: fold))
  place(top + left, dx: x0 + tw, dy: y0 + th + 2mm, line(angle: 90deg, length: 6mm, stroke: fold))
  place(bottom + center, dy: -4mm, caption)
})

#for i in range(calc.quo(n, 4)) {
  spread(n - 2 * i, 2 * i + 1, [Sheet #(i + 1) of #calc.quo(n, 4), front: pages #(n - 2 * i) and #(2 * i + 1)])
  pagebreak()
  spread(2 * i + 2, n - 2 * i - 1, [Sheet #(i + 1) of #calc.quo(n, 4), back (flip on short edge): pages #(2 * i + 2) and #(n - 2 * i - 1)])
  if i < calc.quo(n, 4) - 1 { pagebreak() }
}
