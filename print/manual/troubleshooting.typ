#import "lib.typ": *

= If Something Goes Wrong

#controls(
  ([NO REPLY: MEGAWIFI MODULE NOT FOUND], [The cartridge's Wi-Fi chip didn't answer. Turn the power off and on. If it
    keeps happening, update its firmware (rev-c-1250k).]),
  ([HOLD POSITION. WI-FI FAILED], [Wrong password, too far from the router, or a 5 GHz-only network.
    Setup opens so you can fix it.]),
  ([HOLD POSITION. NO RELAY FOUND], [Is the relay running on the same network? If your router
    blocks mDNS, type the relay's address under RELAY in Setup.]),
  ([ERR HTTP / ERR RECV], [The relay can't be reached. After three misses, AUTO looks for it again;
    a typed address needs checking (port :8080).]),
  ([RELAY MISMATCH / RELAY VER?], [The relay and cartridge are from different
    releases. Run the relay version the message names.]),
  ([ERR FRAME / ERR SIZE], [Something other than the relay answered at that address.]),
  ([UPSTRM ERR], [The traffic feed isn't answering the relay. The last good picture stays up.]),
  ([No aircraft], [Quiet skies, or a small airport. Try a busier airport or zoom out with *B*.]),
)

#note[The Wi-Fi light goes out][If the console never talks to the cartridge, its Wi-Fi chip goes to
sleep after 30 seconds. That's normal: turning the console on wakes it up.]
