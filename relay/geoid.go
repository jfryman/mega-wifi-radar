package main

// The geoid (lode/plans/cockpit-low-altitude.md): ADS-B's geometric altitude is height above the WGS84 ellipsoid,
// terrain is height above sea level (the EGM96 geoid). Around Nashville the geoid is 30 m below the ellipsoid, so
// without this every aircraft in cockpit mode flew 100 ft low. geoid.bin is EGM96 on a 1-degree grid
// (tools/geoidgen): 181 rows from 90N, 361 columns from 0E, int8 metres.

import (
	_ "embed"
	"math"
)

//go:embed geoid.bin
var geoidGrid []byte

// geoidM is the geoid's height above the ellipsoid at lat, lon (degrees), bilinear on the grid.
func geoidM(lat, lon float64) float64 {
	if len(geoidGrid) != 181*361 {
		return 0
	}
	r := (90 - math.Max(-90, math.Min(90, lat)))
	c := math.Mod(lon+360, 360)
	r0, c0 := int(math.Min(r, 179)), int(math.Min(c, 359))
	fr, fc := r-float64(r0), c-float64(c0)
	at := func(r, c int) float64 { return float64(int8(geoidGrid[r*361+c])) }
	return (at(r0, c0)*(1-fc)+at(r0, c0+1)*fc)*(1-fr) + (at(r0+1, c0)*(1-fc)+at(r0+1, c0+1)*fc)*fr
}
