#import "lib.typ": *
#import "../hardware.typ": model1-top, model1-front, cartridge
#import "../keyart.typ": scope

= Starting Up

#align(center, model1-top(88mm, callouts: ((2, 140, 72), (3, 42, 177))))
#v(1mm)
#align(center, model1-front(88mm, callouts: ((1, 106, 23),)))
#v(2mm)

#step(1, [Set up your Genesis system], [as described in its instruction manual. Plug Control Pad 1
  into the port marked *CONTROL 1* on the front of the console.])
#v(1mm)
#step(2, [Make sure the power switch is OFF.], [Then insert the cartridge into the slot, label facing
  toward you, and press it down firmly until it's seated.])
#v(1mm)
#step(3, [Turn the power switch ON.], [The boot screen appears and the cartridge looks for its Wi-Fi
  module, loads your settings and joins your network. Then the radar starts sweeping.])

#pagebreak()
#grid(columns: (30mm, 1fr), column-gutter: 5mm,
  cartridge(28mm, art: (w, h) => {
    place(top + left, rect(width: w, height: h, fill: grid-tiling(cell: 1.4mm)))
    place(top + left, dx: (w - h * 0.8) / 2, dy: h * 0.1, scope(h * 0.8, tags: false, detail: 0.3))
  }),
  [
    == The first time

    A new cartridge doesn't know your Wi-Fi network or where to get its traffic, so the *Setup*
    screen opens by itself. Choose your network, enter the password, type in your relay's
    address and pick your home airport. See _Setting Up_ in this manual.

    == If nothing appears

    Turn the power switch *OFF*. Check that the cartridge is pushed all the way in and that
    the console is connected to your TV properly, then turn the power switch *ON* again.

    #note[Important][Always make sure the power switch is OFF before you insert or remove the
    cartridge.]
  ],
)

== What you will need

- A Sega Genesis or Mega Drive (Model 1 shown).
- This cartridge: a MegaWiFi Rev.C, running Wi-Fi firmware *rev-c-1250k*.
- A 2.4 GHz Wi-Fi network (WPA2). The cartridge can't see 5 GHz-only networks.
- A *relay* running on a computer on the same network. See _Before You Begin: The Relay_.
