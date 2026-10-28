// Key art: the radar scope drawn as vectors from the recorded KBNA traffic
// (relay/testdata/kbna-seq, adsb.fi snapshots 4 s apart), the way the ROM draws it:
// 5 rings, rim ticks, a sweep with afterglow, blips with 5-point trails, two-line tags
// coloured by altitude band. Shared by the label, cover and manual.
#import "theme.typ": ink, font

#let home = (lat: 36.1245, lon: -86.6782)
#let frame(n) = json("/relay/testdata/kbna-seq/kbna-" + (if n < 10 { "0" } else { "" }) + str(n) + ".json")
// Newest first: current position, then one trail dot per 12 s (the ROM keeps one per sweep).
#let frames = (16, 13, 10, 7, 4).map(frame)

// Equirectangular offset in nm (east, north): plenty accurate at 25 nm.
#let project(a) = (
  (a.lon - home.lon) * 60 * calc.cos(home.lat * 1deg),
  (a.lat - home.lat) * 60,
)

#let callsign(a) = {
  let cs = a.at("flight", default: "").trim()
  if cs == "" { cs = a.at("r", default: upper(a.hex)) }
  cs
}

#let band(a) = {
  let alt = a.at("alt_baro", default: "ground")
  if type(alt) == str or alt < 10000 { ink.phosphor } else if alt < 18000 { ink.cyan } else { ink.white }
}

#let tag-lines(a) = {
  let alt = a.at("alt_baro", default: none)
  let a3 = if alt == none { "---" } else if type(alt) == str { "GND" } else {
    let h = calc.round(alt / 100)
    (if h < 100 { "0" } else { "" }) + (if h < 10 { "0" } else { "" }) + str(h)
  }
  let gs = str(calc.round(a.at("gs", default: 0)))
  (callsign(a), a3 + " " + gs)
}

// d: diameter. rim-nm: nm at the rim. sweep: bearing of the sweep line in degrees (0 = north).
// tags: draw data tags. detail: 0..1 scales line weights for small sizes.
// view: (x0, y0, x1, y1) as fractions of d: the part a window shows; tags stay inside it.
#let scope(d, rim-nm: 25, sweep: 52, tags: true, tag-size: 0.016, detail: 1.0,
    view: (0, 0, 1, 1)) = box(width: d, height: d, {
  let r = d / 2
  let px(nm) = nm / rim-nm * r
  let w(x) = x * detail

  // disc
  place(circle(radius: r, fill: gradient.radial(rgb("#06261a"), ink.scope-bg, rgb("#010603"),
    focal-radius: 0%), stroke: w(1.2pt) + ink.phosphor-dim))

  // afterglow: a conic gradient ending at the sweep line, trailing anticlockwise
  let glow = gradient.conic(
    (ink.phosphor.transparentize(100%), 0%),
    (ink.phosphor.transparentize(100%), 72%),
    (ink.phosphor.transparentize(78%), 96%),
    (ink.phosphor.transparentize(40%), 99.6%),
    (ink.phosphor.transparentize(100%), 100%),
    angle: sweep * 1deg + 90deg,
  )
  place(circle(radius: r, fill: glow, stroke: none))

  // rings
  for i in range(1, 5) {
    let rr = r * i / 5
    place(dx: r - rr, dy: r - rr, circle(radius: rr, stroke: w(0.5pt) + ink.phosphor-dim))
  }
  // rim ticks
  for deg in range(0, 360, step: 10) {
    let long = calc.rem(deg, 30) == 0
    let l = if long { r * 0.07 } else { r * 0.035 }
    let (s, c) = (calc.sin(deg * 1deg), calc.cos(deg * 1deg))
    place(line(start: (r + s * (r - l), r - c * (r - l)), end: (r + s * r, r - c * r),
      stroke: w(if long { 0.8pt } else { 0.45pt }) + ink.phosphor-dim))
  }
  // sweep line
  let (s, c) = (calc.sin(sweep * 1deg), calc.cos(sweep * 1deg))
  place(line(start: (r, r), end: (r + s * r, r - c * r), stroke: w(1.4pt) + ink.phosphor))

  // traffic: trails (oldest dimmest), then blips, then tags placed like the ROM does:
  // first free spot of 4 around the blip, skipped if none is free.
  let now = frames.at(0).ac.filter(a => "lat" in a)
    .map(a => (a: a, p: project(a)))
    .filter(t => calc.sqrt(t.p.at(0) * t.p.at(0) + t.p.at(1) * t.p.at(1)) < rim-nm * 0.92)
  let b = d * 0.014
  let fs = d * tag-size
  for t in now {
    for (i, f) in frames.slice(1).enumerate() {
      let old = f.ac.find(o => o.hex == t.a.hex and "lat" in o)
      if old == none { continue }
      let (ox, oy) = project(old)
      let dot = d * 0.0065
      place(top + left, dx: r + px(ox) - dot / 2, dy: r - px(oy) - dot / 2,
        rect(width: dot, height: dot, fill: ink.phosphor.transparentize(30% + i * 14%)))
    }
  }
  let taken = now.map(t => (r + px(t.p.at(0)) - b, r - px(t.p.at(1)) - b, 2 * b, 2 * b))
  // (closures capture by value, so the overlap test is rebuilt after every placement)
  let overlaps(q, o) = q.at(0) < o.at(0) + o.at(2) and o.at(0) < q.at(0) + q.at(2) and q.at(1) < o.at(1) + o.at(3) and o.at(1) < q.at(1) + q.at(3)
  for t in now {
    let (x, y) = (r + px(t.p.at(0)), r - px(t.p.at(1)))
    place(top + left, dx: x - b / 2, dy: y - b / 2, rect(width: b, height: b, fill: band(t.a)))
    let ground = type(t.a.at("alt_baro", default: 0)) == str
    if not tags or ground { continue }
    let (l1, l2) = tag-lines(t.a)
    let tw = calc.max(l1.len(), l2.len()) * fs
    let th = fs * 2.4
    let spots = ((x + b, y - th - b * 0.2), (x + b, y + b * 0.2), (x - b - tw, y - th - b * 0.2), (x - b - tw, y + b * 0.2))
    let placed = taken
    let spot = spots.find(sp => {
      let q = (sp.at(0), sp.at(1), tw, th)
      not placed.any(o => overlaps(q, o)) and sp.at(0) > d * view.at(0) and sp.at(0) + tw < d * view.at(2) and sp.at(1) > d * view.at(1) and sp.at(1) + th < d * view.at(3)
    })
    if spot == none { continue }
    taken.push((spot.at(0), spot.at(1), tw, th))
    place(top + left, dx: spot.at(0), dy: spot.at(1),
      text(font: font.pixel, size: fs, fill: band(t.a), top-edge: "cap-height",
        bottom-edge: "baseline", { set par(leading: fs * 0.4); l1 + linebreak() + l2 }))
  }
  // centre mark
  place(dx: r - d * 0.006, dy: r - d * 0.006, circle(radius: d * 0.006, fill: ink.phosphor-dim))
})
