#import "lib.typ": *

= If Something Goes Wrong

#controls(
  ([MegaWiFi module not found], [The cartridge's Wi-Fi chip didn't answer. Turn the power off and
    on again. If it keeps happening, its firmware needs updating (rev-c-1250k).]),
  ([Wi-Fi failed: setup], [Wrong password, too far from the router, or a 5 GHz-only network.
    Setup opens so you can fix it.]),
  ([No relay set: setup], [Enter the relay's address in Setup.]),
  ([ERR HTTP / ERR RECV], [The relay can't be reached. Check that it's running, the address and
    the port (:8080), and use the full name or the IP address.]),
  ([ERR FRAME / ERR SIZE], [Something other than the relay answered at that address, or the relay
    is newer than the cartridge.]),
  ([UPSTRM ERR], [The relay is fine, but the traffic feed isn't answering it. It keeps showing the
    last good picture.]),
  ([No aircraft], [Quiet skies, or a small airport. Try a busier airport or zoom out with *B*.]),
)

#note[The Wi-Fi light goes out][If the console never talks to the cartridge, its Wi-Fi chip goes to
sleep after 30 seconds. That's normal: turning the console on wakes it up.]
