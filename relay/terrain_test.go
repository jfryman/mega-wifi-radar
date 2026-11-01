package main

import (
	"context"
	"errors"
	"math"
	"testing"
	"time"
)

// flatDEM is ground at a fixed elevation everywhere.
type flatDEM float64

func (f flatDEM) elevation(context.Context, int, float64, float64) (float64, error) {
	return float64(f), nil
}

// rampDEM rises 1 m per 100 m going east from 100 m at lon 0.
type rampDEM struct{}

func (rampDEM) elevation(_ context.Context, _ int, lat, lon float64) (float64, error) {
	return 100 + lon*metresPerDegN*math.Cos(lat*math.Pi/180)/100, nil
}

type failDEM struct{}

func (failDEM) elevation(context.Context, int, float64, float64) (float64, error) {
	return 0, errors.New("offline")
}

func TestKeyRoundTrip(t *testing.T) {
	g := geom{lat: 36.12345, lon: -86.67891, cellM: 183, levelCm: 1144, baseM: -12, hiM: 430}
	got, err := parseKey(g.key())
	if err != nil || got != g {
		t.Fatalf("parseKey(%q) = %+v, %v", g.key(), got, err)
	}
	if len(g.key()) >= keyLen {
		t.Errorf("key %q doesn't fit AR_KEY_LEN with its NUL", g.key())
	}
	for _, bad := range []string{"", "1,2,3,4,5", "91,0,100,100,0,0", "1,2,0,100,0,0", "1,2,100,100,50,0", "a,b,c,d,e,f"} {
		if _, err := parseKey(bad); err == nil {
			t.Errorf("parseKey(%q) accepted", bad)
		}
	}
}

func TestCellGeometry(t *testing.T) {
	g := geom{lat: 36.1, lon: -86.7, cellM: 200, levelCm: 1250, baseM: 100}
	for _, cr := range [][2]int{{0, 0}, {64, 64}, {127, 3}} {
		la, lo := g.latLon(cr[0], cr[1])
		x, y := g.cellXY(la, lo)
		if math.Abs(x-float64(cr[0])-0.5) > 1e-6 || math.Abs(y-float64(cr[1])-0.5) > 1e-6 {
			t.Errorf("cell %v -> %.5f,%.5f -> %.4f,%.4f", cr, la, lo, x, y)
		}
	}
	if la, _ := g.latLon(64, 100); la <= g.lat {
		t.Errorf("rows must run north")
	}
	if _, lo := g.latLon(100, 64); lo <= g.lon {
		t.Errorf("columns must run east")
	}
	if l := g.level(100 + 12.5*10); math.Abs(l-10) > 1e-9 {
		t.Errorf("level(225 m) = %v, want 10", l)
	}
}

func TestBuildPatch(t *testing.T) {
	ctx := context.Background()
	// 9,000 ft over 150 m ground: about 519 m cells, true-scale levels (cell / 16), flat.
	p := buildPatch(ctx, flatDEM(150), 36.1, -86.7, 9000*0.3048, 0)
	if p.g.cellM != cellFor(9000*0.3048-150) || p.g.cellM < 500 || p.g.cellM > 540 {
		t.Errorf("cell %d m", p.g.cellM)
	}
	if p.g.levelCm != int(math.Ceil(float64(p.g.cellM)/16*100)) || p.g.baseM != 150 || p.hmax != 0 || p.flags != 0 {
		t.Errorf("flat patch: level %d cm, base %d, hmax %d, flags %#x", p.g.levelCm, p.g.baseM, p.hmax, p.flags)
	}
	// On the ground: the smallest cells.
	if p := buildPatch(ctx, flatDEM(150), 36.1, -86.7, math.NaN(), 0); p.g.cellM != minCellM {
		t.Errorf("ground cell %d m", p.g.cellM)
	}
	// A ramp: heights rise eastwards and fill the levels; west of it all land.
	p = buildPatch(ctx, rampDEM{}, 0.5, 0.2, 5000, 0)
	if p.height[64*tn+127] <= p.height[64*tn] || p.hmax == 0 || int(p.hmax) >= levels {
		t.Errorf("ramp: west %d, east %d, hmax %d", p.height[64*tn], p.height[64*tn+127], p.hmax)
	}
	for i, c := range p.colour {
		if c < 4 || c > 15 {
			t.Fatalf("colour[%d] = %d: outside the ground palette", i, c)
		}
	}
	// Offline: flat ground, flagged, still a full patch.
	if p := buildPatch(ctx, failDEM{}, 36.1, -86.7, 3000, 0); p.flags&tFlat == 0 || p.hmax != 0 ||
		len(p.encode()) != terrainSize {
		t.Errorf("offline patch: flags %#x hmax %d", p.flags, p.hmax)
	}
	// Sea: below 0 m is water at level 0.
	if p := buildPatch(ctx, flatDEM(-40), 36.1, -86.7, 3000, 0); p.colour[0] != cWater && p.colour[0] != cWater+1 {
		t.Errorf("sea colour %d", p.colour[0])
	}
}

func TestBuildTrack(t *testing.T) {
	ctx := context.Background()
	g := geom{lat: 36.1, lon: -86.7, cellM: 200, levelCm: 1250, baseM: 100}
	la, lo := g.latLon(74, 64) // 10 cells east of the centre cell
	gs, trk, alt, rate := 200.0, 90.0, 5000.0, -1000.0
	a := upstreamAC{Hex: "abcdef", Flight: "TEST1  ", Lat: &la, Lon: &lo, AltBaro: alt, GS: &gs, Track: &trk,
		BaroRate: &rate}
	tr := newTurns()
	k := buildTrack(ctx, &a, g, flatDEM(150), tr, "abcdef", time.Unix(1000, 0))
	if k.flags != kFound || math.Abs(float64(k.x)/256-74.5) > 0.01 || math.Abs(float64(k.y)/256-64.5) > 0.01 {
		t.Errorf("track %+v", k)
	}
	if want := (alt*0.3048 - 100) / 12.5; math.Abs(float64(k.z)/256-want) > 0.01 {
		t.Errorf("z %.2f levels, want %.2f", float64(k.z)/256, want)
	}
	if want := 200 * 0.514444 / 200; math.Abs(float64(k.vx)/256-want) > 0.01 || k.vy != 0 {
		t.Errorf("v %d,%d, want %.3f cells/s east", k.vx, k.vy, want)
	}
	if k.vz >= 0 || k.vrate != -1000 || k.heading != 0x4000 || k.alt != 200 || k.gs != 200 || k.bank != 0 {
		t.Errorf("track %+v", k)
	}
	// Turning right 3 deg/s at 200 kt: about 28 deg of bank, estimated from the previous sighting.
	trk2 := 102.0
	a.Track = &trk2
	k = buildTrack(ctx, &a, g, flatDEM(150), tr, "abcdef", time.Unix(1004, 0))
	if k.bank < 25 || k.bank > 32 {
		t.Errorf("estimated bank %d", k.bank)
	}
	// On the ground: eye height over the ground, no bank, no vertical speed.
	a.AltBaro = "ground"
	k = buildTrack(ctx, &a, g, flatDEM(150), newTurns(), "abcdef", time.Unix(1000, 0))
	if k.flags&kGround == 0 || math.Abs(float64(k.z)/256-(150+eyeM-100)/12.5) > 0.01 || k.vz != 0 || k.bank != 0 {
		t.Errorf("ground track %+v", k)
	}
}

// A strip from inside the patch is the patch, cell for cell; past its edge the grid goes on. The strip's
// geometry comes from the key, as the cart sends it (an off-grid centre: the key rounds it).
func TestStripMatchesPatch(t *testing.T) {
	ctx := context.Background()
	p := buildPatch(ctx, rampDEM{}, 0.5000037, 0.2000041, 5000, 0)
	g, err := parseKey(p.g.key())
	if err != nil {
		t.Fatal(err)
	}
	p.g = g
	const x0, y0, w, h = 10, 20, 8, 100
	c, err := sampleCells(ctx, rampDEM{}, p.g, x0, y0, w, h)
	if err != nil {
		t.Fatal(err)
	}
	height, colour := make([]uint8, w*h), make([]uint8, w*h)
	p.g.fill(&c, false, height, colour)
	for r := 0; r < h; r++ {
		for k := 0; k < w; k++ {
			i := (y0+r)*tn + x0 + k
			if height[r*w+k] != p.height[i] || colour[r*w+k] != p.colour[i] {
				t.Fatalf("cell %d,%d: strip %d/%d, patch %d/%d", x0+k, y0+r, height[r*w+k], colour[r*w+k],
					p.height[i], p.colour[i])
			}
		}
	}
	// East of the patch the ramp keeps rising (until it tops out at the last level).
	c, _ = sampleCells(ctx, rampDEM{}, p.g, tn, 64, 8, 1)
	p.g.fill(&c, false, height[:8], colour[:8])
	if height[7] < p.height[64*tn+127] {
		t.Errorf("past the east edge: %d, edge %d", height[7], p.height[64*tn+127])
	}
}
