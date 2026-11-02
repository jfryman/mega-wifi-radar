package main

// Cockpit mode's sky follows the real sun (lode/ui/cockpit.md): the terrain patch's palette (struct
// ar_terrain.pal, PAL0 on the cart) gets a sky and a ground brightness for the sun's elevation at the patch, at
// the time of the request. The cart takes the palette as it comes, so this is relay-only.

import (
	"fmt"
	"math"
	"time"
)

type skyPhase int

const (
	skyDay      skyPhase = iota // sun above 6°
	skyGolden                   // 0..6°: low sun
	skyTwilight                 // -6..0°: civil twilight
	skyNight                    // below -6°
)

var skyNames = map[string]skyPhase{"day": skyDay, "golden": skyGolden, "twilight": skyTwilight, "night": skyNight}

// skyOverride forces a phase (the -sky flag, RELAY_SKY: for tests and screenshots); -1 follows the sun.
var skyOverride skyPhase = -1

func parseSky(s string) (skyPhase, error) {
	if s == "" || s == "real" {
		return -1, nil
	}
	if p, ok := skyNames[s]; ok {
		return p, nil
	}
	return -1, fmt.Errorf("sky %q: want real, day, golden, twilight or night", s)
}

// Per phase: the three sky entries (deep, mid, pale at the horizon), how bright the ground is, a blue lift for
// the ground (night reads blue-grey, not brown), the colour of cells under city lights (0: shaded like the
// ground, grey by day: also runway edge and approach lights), and the runways' at approach scale (concrete by
// day, dark after dusk between their lights).
var skies = [...]struct {
	sky    [3]uint16
	ground float64
	blue   uint16
	urban  uint16
	runway uint16
}{
	skyDay:      {[3]uint16{0xA42, 0xC86, 0xECA}, 1, 0, 0, 0x666},
	skyGolden:   {[3]uint16{0xA64, 0xA8A, 0x6CE}, 0.85, 0, 0, 0x666},
	skyTwilight: {[3]uint16{0x622, 0x846, 0x48E}, 0.55, 0, 0x26A, 0x444},
	skyNight:    {[3]uint16{0x200, 0x400, 0x622}, 0.3, 2, 0x2AE, 0x222},
}

// sunElevation is the sun's elevation in degrees at lat, lon (degrees) and t: the NOAA low-precision formulas,
// good to a few tenths of a degree, plenty for picking a sky.
func sunElevation(lat, lon float64, t time.Time) float64 {
	rad := math.Pi / 180
	d := float64(t.UTC().UnixNano())/86400e9 - 10957.5 // days since J2000.0 (2000-01-01 12:00 UTC)
	g := (357.529 + 0.98560028*d) * rad                // mean anomaly
	q := 280.459 + 0.98564736*d                        // mean longitude, degrees
	l := (q + 1.915*math.Sin(g) + 0.020*math.Sin(2*g)) * rad
	e := (23.439 - 0.00000036*d) * rad // obliquity
	ra := math.Atan2(math.Cos(e)*math.Sin(l), math.Cos(l))
	dec := math.Asin(math.Sin(e) * math.Sin(l))
	gmst := math.Mod(18.697374558+24.06570982441908*d, 24) // hours
	ha := (gmst*15+lon)*rad - ra
	el := math.Asin(math.Sin(lat*rad)*math.Sin(dec) + math.Cos(lat*rad)*math.Cos(dec)*math.Cos(ha))
	return el / rad
}

func phaseAt(lat, lon float64, t time.Time) skyPhase {
	if skyOverride >= 0 {
		return skyOverride
	}
	switch el := sunElevation(lat, lon, t); {
	case el > 6:
		return skyDay
	case el > 0:
		return skyGolden
	case el > -6:
		return skyTwilight
	default:
		return skyNight
	}
}

// skyPalette is terrainPal with the phase's sky and the ground (4-15) dimmed.
func skyPalette(p skyPhase) [16]uint16 {
	pal := terrainPal
	copy(pal[1:4], skies[p].sky[:])
	for i := cWater; i < len(pal); i++ {
		pal[i] = shade(pal[i], skies[p].ground)
		if b := pal[i] >> 8; b < skies[p].blue {
			pal[i] = pal[i]&0x0FF | skies[p].blue<<8
		}
	}
	if skies[p].urban != 0 {
		pal[cUrban] = skies[p].urban
	}
	return pal
}

// shade scales a 0x0BGR colour, keeping each channel on the VDP's even steps (0, 2 .. E).
func shade(c uint16, f float64) uint16 {
	var out uint16
	for s := 0; s < 12; s += 4 {
		v := float64(c >> s & 0xF)
		out |= uint16(math.Round(v*f/2)*2) << s
	}
	return out
}

// patchPalette is a patch's palette: the phase's, and at approach scale the runway entry (terrain.go cRunway)
// in place of snow. Low down by day, city-light cells are ground: at 60 m cells the lights pattern around an
// airport is a grey mosaic that buries the runways (they light up again after dusk).
func patchPalette(p skyPhase, cellM int) [16]uint16 {
	pal := skyPalette(p)
	if cellM <= approachCellM {
		pal[cRunway] = skies[p].runway
		if skies[p].urban == 0 {
			pal[cUrban] = pal[cLow+1]
		}
	}
	return pal
}
