#import "lib.typ": *
#import "../hardware.typ": pad

= Take Control!

#align(center, pad(64mm, callouts: ((1, 46, 40), (2, 91, 64), (3, 122, 78), (4, 139, 66), (5, 156, 54))))
#v(1mm)

#key(1)[D-Button][Pan the scope up, down, left and right. In menus, move between rows and change
  values.]
#key(2)[Start][Open the Setup screen. In Setup, save your settings and go back to the radar.]
#key(3)[Button A][Zoom in. In Setup, edit the selected row (type an airport code, pick a network,
  change the relay).]
#key(4)[Button B][Zoom out. On the keyboard, delete the last letter.]
#key(5)[Button C][Select the next aircraft, nearest first; its details appear in the side panel.
  *Hold C* to change which data tags are shown: all, selected only, or none.]

== Range steps

#grid(columns: (1fr, 44mm), column-gutter: 3mm,
  [The scope zooms between *5, 10, 25, 50 and 100* nautical miles from your home airport. Each scope
  has five range rings, and the outermost ring is the range shown in the panel (*RNG*). Here, *A*
  has zoomed in twice to 5 nm, with rings every mile.],
  shot("zoom", width: 44mm),
)
