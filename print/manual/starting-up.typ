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
#step(3, [Turn the power switch ON.], [Your flight, *MEGA 787*, appears at the gate. Follow its
  radio calls as the cartridge finds its Wi-Fi module and joins your network. When the tower
  clears it for takeoff, the radar starts sweeping.])

#pagebreak()
== The departure

#align(center, shot("boot-takeoff", width: 88%))
#v(1mm)
Each radio call is a real step: *radio check* (finding the Wi-Fi module), *taxi* (settings
loaded), *hold short* (joining Wi-Fi), *request radar service* (finding the relay), *cleared for
takeoff* (connected). Press *Start* to skip ahead.

#grid(columns: (26mm, 1fr), column-gutter: 5mm,
  cartridge(24mm, art: (w, h) => {
    place(top + left, rect(width: w, height: h, fill: grid-tiling(cell: 1.4mm)))
    place(top + left, dx: (w - h * 0.8) / 2, dy: h * 0.1, scope(h * 0.8, tags: false, detail: 0.3))
  }),
  [
    == The first time

    A new cartridge doesn't know your Wi-Fi network yet, so the tower says *hold position* and
    the *Setup* screen opens by itself. Choose your network, enter the password and pick your home
    airport. The relay is found automatically. See _Setting Up_.

    == If nothing appears

    Turn the power switch *OFF*. Check that the cartridge is pushed all the way in and that the
    console is connected to your TV properly, then turn the power switch *ON* again.
  ],
)
