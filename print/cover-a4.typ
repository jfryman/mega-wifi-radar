// The cover wrap on A4 landscape for home printing (Letter is too narrow: the wrap is 286 mm + bleed).
// Places cover-trim.pdf (trim + bleed, no marks) centred, then draws the trim and fold marks in the
// top and bottom margins, plus short white ticks in the side bleed for the top and bottom trim lines.
// Built by scripts/print after the cover. Print at 100% ("actual size").
#import "theme.typ": *

#let (fw, h) = size-of("cover")
#let sp = dims.cover.spine * 1mm
#let w = 2 * fw + sp
#let (pw, ph) = (297mm, 210mm)
#let (ox, oy) = ((pw - w) / 2, (ph - h) / 2)          // trim corner on the sheet

#set page(width: pw, height: ph, margin: 0mm, background: {
  place(top + left, dx: ox - bleed, dy: oy - bleed,
    image("/out/print/cover-trim.pdf", width: w + 2 * bleed, height: h + 2 * bleed))
  let st = 0.3pt + black
  let len = oy - bleed - 3mm
  for x in (0mm, fw, fw + sp, w) {
    let s = if x == 0mm or x == w { st } else { (paint: black, thickness: 0.3pt, dash: (1.2pt, 1.2pt)) }
    place(top + left, dx: ox + x, dy: 1.5mm, line(angle: 90deg, length: len, stroke: s))
    place(top + left, dx: ox + x, dy: oy + h + bleed + 1.5mm, line(angle: 90deg, length: len, stroke: s))
  }
  for y in (0mm, h) {
    for x in (ox - bleed, ox + w) {
      place(top + left, dx: x, dy: oy + y, line(length: bleed, stroke: 0.4pt + white))
    }
  }
  place(top + left, dx: ox, dy: ph - 4mm, text(font: font.body, size: 6pt, fill: black,
    [Mega WiFi Radar cover wrap #version · print at 100% on A4 landscape · solid lines: cut · dashed: fold]))
})
#box()
