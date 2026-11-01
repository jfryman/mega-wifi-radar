#import "lib.typ": *

// The 32X edition only (manual.typ includes it when edition = 32x).
= The 32X Edition

#grid(columns: (1fr, auto), column-gutter: 4mm, align: horizon,
  [This edition runs the same radar on the Sega 32X. The Genesis inside still talks to the Wi-Fi
  cartridge and reads your control pad; the 32X's two processors draw everything you see, in 256
  colours at a time out of 32,768.],
  mark32x(h: 12mm),
)

== What's new on the 32X

- *The scope:* a smooth disc with a long phosphor afterglow behind the sweep, glowing targets that
  flare as the sweep passes, and leader lines from every tag to its aircraft.
- *The cockpit view:* twice the detail across, haze that thickens with distance, a sky that banks
  with the horizon, and a heading tape along the top.
- *The departure:* a dawn airfield to watch while the cartridge connects.

#align(center, shot("cockpit", width: 78%))

== Settings are shared

Wi-Fi networks and your settings live in the cartridge's Wi-Fi chip, not the game, so the Genesis
and 32X editions share them. Set up once in either.

#note[Turn the power off first][Always switch the console off before attaching or removing the 32X or
the cartridge. Without a 32X, this edition shows nothing: use the Genesis edition instead.]
