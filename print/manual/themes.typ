#import "lib.typ": *

= Themes

Choose a look in Setup under *THEME* (or press *X* on a six-button pad).

== Classic

#align(center, shot("radar", width: 58%))
Green phosphor on black, with the thin-line sweep and its afterglow. Blips flare as the sweep
passes and fade until the next pass, like the big round scopes in a 1980s tower.

== STARS

#align(center, shot("stars", width: 58%))
In the style of modern terminal radar displays: no sweep, blue rings, and targets at constant
brightness that simply move on each update. Each tag is a data block set off on a short leader line.

#pagebreak()

== More looks

#let tile(name, title, body) = block(breakable: false, {
  shot("theme-" + name)
  v(0.6mm)
  text(weight: "bold", title)
  linebreak()
  text(size: 0.85em, body)
})

#grid(columns: (1fr, 1fr, 1fr), column-gutter: 2.5mm, row-gutter: 3.5mm,
  tile("amber", [AMBER], [Amber phosphor, an orange sweep.]),
  tile("night", [NIGHT], [Dim reds for a dark room.]),
  tile("marine", [MARINE], [A ship's radar: yellow on navy.]),
  tile("ice", [ICE], [Blue-white phosphor.]),
  tile("pocket", [POCKET], [A handheld's green LCD. No sweep.]),
  tile("sunset", [SUNSET], [Cyan sweep over a purple dusk.]),
  tile("eram", [ERAM], [An en-route centre display, with leader lines.]),
)

Every theme keeps the same tag colours by meaning: one colour each for low, middle and high
traffic, and emergencies stand out in red (pale pink in *NIGHT*, brown in *POCKET*).
