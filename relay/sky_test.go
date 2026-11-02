package main

import (
	"math"
	"testing"
	"time"
)

// KBNA: solar noon elevation = 90 - lat + declination.
func TestSunElevation(t *testing.T) {
	const lat, lon = 36.1245, -86.6782
	for _, c := range []struct {
		at   string
		want float64
		tol  float64
	}{
		{"2026-06-21T17:48:00Z", 90 - lat + 23.44, 0.6}, // summer solstice, solar noon
		{"2026-03-20T17:54:00Z", 90 - lat, 0.8},         // equinox, solar noon
		{"2026-12-21T17:53:00Z", 90 - lat - 23.44, 0.6}, // winter solstice, solar noon
	} {
		at, _ := time.Parse(time.RFC3339, c.at)
		if got := sunElevation(lat, lon, at); math.Abs(got-c.want) > c.tol {
			t.Errorf("%s: elevation %.2f, want %.2f ± %.1f", c.at, got, c.want, c.tol)
		}
	}
	midnight, _ := time.Parse(time.RFC3339, "2026-06-21T05:48:00Z")
	if got := sunElevation(lat, lon, midnight); got > -20 {
		t.Errorf("local midnight: elevation %.1f, want well below the horizon", got)
	}
}

func TestPhaseAt(t *testing.T) {
	const lat, lon = 36.1245, -86.6782
	for at, want := range map[string]skyPhase{
		"2026-06-21T17:48:00Z": skyDay,
		"2026-06-22T01:30:00Z": skyTwilight, // ~20:30 CDT, just after a 20:08 sunset
		"2026-06-22T05:00:00Z": skyNight,
	} {
		tm, _ := time.Parse(time.RFC3339, at)
		if got := phaseAt(lat, lon, tm); got != want {
			t.Errorf("%s: phase %d, want %d (elevation %.1f)", at, got, want, sunElevation(lat, lon, tm))
		}
	}
}

func TestSkyPalette(t *testing.T) {
	if skyPalette(skyDay) != terrainPal {
		t.Error("day palette must be terrainPal unchanged")
	}
	for p := skyDay; p <= skyNight; p++ {
		for i, c := range skyPalette(p) {
			if c&0x111 != 0 || c > 0xEEE {
				t.Errorf("phase %d colour %d = %#03x: not a VDP colour (even nibbles)", p, i, c)
			}
		}
	}
	night := skyPalette(skyNight)
	if night[cLow+2] >= terrainPal[cLow+2] {
		t.Errorf("night ground %#03x not darker than day %#03x", night[cLow+2], terrainPal[cLow+2])
	}
}

func TestParseSky(t *testing.T) {
	if p, err := parseSky("real"); err != nil || p != -1 {
		t.Errorf("real: %v %v", p, err)
	}
	if p, err := parseSky("night"); err != nil || p != skyNight {
		t.Errorf("night: %v %v", p, err)
	}
	if _, err := parseSky("noon"); err == nil {
		t.Error("noon: want an error")
	}
}
