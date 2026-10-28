#import "lib.typ": *

= If Something Goes Wrong

#controls(
  ([NO REPLY: MEGAWIFI MODULE NOT FOUND], [The 787 stays at the gate: the cartridge's Wi-Fi chip didn't answer. Turn the power off and
    on again. If it keeps happening, its firmware needs updating (rev-c-1250k).]),
  ([HOLD POSITION. WI-FI FAILED], [Wrong password, too far from the router, or a 5 GHz-only network.
    Setup opens so you can fix it.]),
  ([HOLD POSITION. NO RELAY FOUND], [No relay answered. Check that it's running on the same
    network. If your router blocks mDNS, type the relay's address under RELAY in Setup.]),
  ([ERR HTTP / ERR RECV], [The relay can't be reached. After three misses in a row the cartridge
    looks for it again (AUTO). With a typed address, check the address and port (:8080).]),
  ([ERR FRAME / ERR SIZE], [Something other than the relay answered at that address, or the relay
    is newer than the cartridge.]),
  ([UPSTRM ERR], [The relay is fine, but the traffic feed isn't answering it. It keeps showing the
    last good picture.]),
  ([No aircraft], [Quiet skies, or a small airport. Try a busier airport or zoom out with *B*.]),
)

#note[The Wi-Fi light goes out][If the console never talks to the cartridge, its Wi-Fi chip goes to
sleep after 30 seconds. That's normal: turning the console on wakes it up.]
