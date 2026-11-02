package main

import (
	"math"
	"testing"
)

// EGM96 from GeographicLib's 5-minute grid; the 1-degree grid is within a few metres away from mountains.
func TestGeoid(t *testing.T) {
	for _, c := range []struct {
		name           string
		lat, lon, want float64
	}{
		{"KBNA", 36.1245, -86.6782, -30.0},
		{"KORD", 41.98, -87.9, -34.1},
		{"KDEN", 39.86, -104.67, -18.4},
		{"KSFO", 37.62, -122.38, -32.3},
		{"date line", 0, 179.99, 21.2},
		{"north pole", 90, 0, 13.6},
	} {
		if got := geoidM(c.lat, c.lon); math.Abs(got-c.want) > 3 {
			t.Errorf("%s: geoid %.1f m, want %.1f", c.name, got, c.want)
		}
	}
}
