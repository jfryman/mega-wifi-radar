#import "lib.typ": *
#import "../hardware.typ": pad

= Take Control!

#align(center, pad(64mm, callouts: ((1, 46, 40), (2, 91, 64), (3, 122, 78), (4, 139, 66), (5, 156, 54))))
#v(1mm)

#key(1)[D-Button][*Right* zooms in, *left* zooms out. *Up* and *down* move through the aircraft in the
  traffic list; hold to scroll. In menus, move between rows and change values.]
#key(2)[Start][Open the Setup screen. In Setup, save your settings and go back to the radar.]
#key(3)[Button A][The next tab in the side panel: *NEAREST*, *LOWEST*, *HIGHEST*, *A-Z*, *LOGBOOK*. In
  _Aircraft Details_, ride along in the _Cockpit View_. In Setup, edit the selected row.]
#key(4)[Button B][The previous tab. Closes the aircraft details, the logbook page and the cockpit view.
  On the keyboard, delete the last letter.]
#key(5)[Button C][Show everything about the selected aircraft (_Aircraft Details_); on the *LOGBOOK* tab,
  the whole day's log. Press *C* or *B* to close it.]

#note[Six-button controller][*X* switches to the next theme, and *Y* opens the cockpit view for the
selected aircraft. *Z* turns the weather on and off. Everything works on the three-button controller; the Setup
screen's *CONTROLS* page lists these buttons too.]

== Range steps

#grid(columns: (1fr, 44mm), column-gutter: 3mm,
  [The scope zooms between *5, 10, 25, 50 and 100* nautical miles from your home airport. Each scope
  has five range rings, and the outermost ring is the range shown in the panel (*RNG*). Here,
  *right* has zoomed in twice to 5 nm, with rings every mile.],
  shot("zoom", width: 44mm),
)

== Ambient mode

Leave the radar alone and it can run itself, like a display in an airport lobby. Set *AMBIENT* in
Setup to 1, 5 or 15 minutes: after that long without a button, it picks a flight (an emergency first,
then low traffic near your airport), shows its details, rides along in the Cockpit View for a little
over a minute, and comes back to the radar. A minute later it picks another. The panel says
*NET OK AUTO* while it does. Press any button to take over again; that press does nothing else.
