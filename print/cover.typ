// Front cover for the clamshell sleeve: 1989-style black grid, top band with our wordmark
// and the red "16-BIT" strip, framed key art, the title, and the platform line.
#import "theme.typ": *
#import "front.typ": front-art

#let (w, h) = size-of("cover")
#show: printed.with(w, h, art: _ => front-art(w, h))
