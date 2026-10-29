// Clamshell sleeve insert, one wrap: back | spine | front (lode/print/print-assets.md).
// The front art is front.typ (also the manual's page 1); back and spine are back.typ.
// Crop marks at the outer trim, dashed fold marks at both spine edges.
#import "theme.typ": *
#import "front.typ": front-art
#import "back.typ": back-art, spine-art

#let (fw, h) = size-of("cover")
#let sp = dims.cover.spine * 1mm
#let w = 2 * fw + sp

// Each face's art includes its own bleed; the spine is drawn last, over the faces' inner bleeds.
#show: printed.with(w, h, folds: (fw, fw + sp), art: _ => {
  place(top + left, back-art(fw, h))
  place(top + left, dx: fw + sp, front-art(fw, h))
  place(top + left, dx: bleed + fw, spine-art(sp, h))
})
