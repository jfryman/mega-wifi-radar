package main

import (
	"context"
	"errors"
	"math"
	"net/http/httptest"
	"strconv"
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
	// On the ground and below 500 m above it: one approach scale, whatever the height.
	if p := buildPatch(ctx, flatDEM(150), 36.1, -86.7, math.NaN(), 0); p.g.cellM != approachCellM {
		t.Errorf("ground cell %d m", p.g.cellM)
	}
	for _, agl := range []float64{10, 100, 499} {
		if c := cellFor(agl); c != approachCellM {
			t.Errorf("%.0f m above ground: %d m cells, want %d", agl, c, approachCellM)
		}
	}
	if c := cellFor(approachAGLM); c != approachAGLM/cellsAbove {
		t.Errorf("at %d m: %d m cells", approachAGLM, c)
	}
	// Flat ground at approach scale is textured: all three low shades, in 2 x 2 fields (away from airports).
	p = buildPatch(ctx, flatDEM(150), 35.5, -86.0, 300, 0)
	seen := map[uint8]int{}
	for i, c := range p.colour {
		seen[c]++
		if r, k := i/tn, i%tn; c != p.colour[(r&^1)*tn+k&^1] {
			t.Fatalf("cell %d,%d isn't its field's colour", k, r)
		}
	}
	if len(seen) != 3 || seen[cLow] == 0 || seen[cLow+1] == 0 || seen[cLow+2] == 0 {
		t.Errorf("flat approach ground: colours %v, want the three low shades", seen)
	}
	p = buildPatch(ctx, flatDEM(150), 35.5, -86.0, 3000, 0)
	for _, c := range p.colour {
		if c != cLow+1 {
			t.Fatalf("flat ground at %d m cells: colour %d, want plain %d", p.g.cellM, c, cLow+1)
		}
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

// Near the ground: GPS altitude loses the geoid (-30 m at KBNA), barometric altitude takes the crew's altimeter
// setting, and a reported altitude under the terrain is lifted to eye height over it.
func TestTrackAltitude(t *testing.T) {
	ctx := context.Background()
	g := geom{lat: 36.1245, lon: -86.6782, cellM: 60, levelCm: 375, baseM: 150}
	la, lo := g.latLon(64, 64)
	z := func(a upstreamAC) float64 {
		a.Lat, a.Lon = &la, &lo
		return float64(buildTrack(ctx, &a, g, flatDEM(180), newTurns(), "abcdef", time.Unix(1000, 0)).z)/256*3.75 + 150
	}
	geomFt := 1000.0
	if got, want := z(upstreamAC{AltGeom: &geomFt, AltBaro: 900.0}), 1000*0.3048+30; math.Abs(got-want) > 2 {
		t.Errorf("GPS altitude: %.1f m above sea level, want %.1f", got, want)
	}
	qnh := 1023.25
	if got, want := z(upstreamAC{AltBaro: 1000.0, NavQNH: &qnh}), (1000+300)*0.3048; math.Abs(got-want) > 1 {
		t.Errorf("baro altitude at QNH 1023: %.1f m, want %.1f", got, want)
	}
	if got, want := z(upstreamAC{AltBaro: 1000.0}), 1000*0.3048; math.Abs(got-want) > 1 {
		t.Errorf("baro altitude without QNH: %.1f m, want %.1f", got, want)
	}
	low := 300.0 // 91 m: under the 180 m ground
	if got, want := z(upstreamAC{AltGeom: &low}), 180+eyeM; math.Abs(got-want) > 0.5 {
		t.Errorf("under the ground: %.1f m, want %.1f (eye height over it)", got, want)
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

// Runways are cells under their centrelines, in their own colour at approach scale (city lights' above it):
// every KBNA runway's midpoint, at least a cell wide, and nothing far from them.
func TestRunwayCells(t *testing.T) {
	ctx := context.Background()
	p := buildPatch(ctx, flatDEM(180), 36.1245, -86.6782, math.NaN(), 0) // on the ground: approach scale
	n := 0
	for _, r := range runways {
		if r.airport != "KBNA" {
			continue
		}
		n++
		x, y := p.g.cellXY((r.lat1+r.lat2)/2, (r.lon1+r.lon2)/2)
		if c := p.colour[int(y)*tn+int(x)]; c != cRunway {
			t.Errorf("runway %s/%s midpoint cell %d,%d: colour %d, want %d", r.le, r.he, int(x), int(y), c, cRunway)
		}
	}
	if n < 3 {
		t.Fatalf("%d KBNA runways in runways.csv", n)
	}
	urban := 0
	for _, c := range p.colour {
		if c == cRunway {
			urban++
		}
	}
	// Approach lights 300 m past 20R's end on the extended centreline, edge lights beside 02L/20R's middle (just
	// outside the surface), no lights at all 4 km from any runway on flat dark ground.
	for _, r := range runways {
		if r.airport != "KBNA" || r.he != "20R" {
			continue
		}
		x1, y1 := p.g.cellXY(r.lat1, r.lon1)
		x2, y2 := p.g.cellXY(r.lat2, r.lon2)
		l := math.Hypot(x2-x1, y2-y1)
		ux, uy := (x2-x1)/l, (y2-y1)/l
		ax, ay := x2+ux*300/60, y2+uy*300/60
		lights := 0 // dotted: some of the cells on the next 4 of the extended centreline
		for _, d := range []float64{0, 1, 2, 3, 4} {
			lights += btoi(p.colour[int(ay+uy*d)*tn+int(ax+ux*d)] == cUrban)
		}
		if lights == 0 {
			t.Errorf("no approach light around cell %d,%d", int(ax), int(ay))
		}
		edge, lit := 0, 0 // cells whose centres are 1.2-1.9 cells beside the runway, along its length
		for i, c := range p.colour {
			px, py := float64(i%tn)+0.5, float64(i/tn)+0.5
			along, side := (px-x1)*ux+(py-y1)*uy, math.Abs((px-x1)*uy-(py-y1)*ux)
			if along > 2 && along < l-2 && side > 1.2 && side < 1.9 {
				edge++
				if c == cUrban {
					lit++
				}
			}
		}
		if edge < 20 || lit < edge*4/10 || lit > edge*6/10 {
			t.Errorf("edge lights: %d of %d cells beside 20R, want about half (dotted)", lit, edge)
		}
	}
	if pal := patchPalette(skyNight, p.g.cellM); pal[cRunway] != skies[skyNight].runway {
		t.Errorf("approach palette: entry %d is %#x, want the night runway", cRunway, pal[cRunway])
	}
	if pal := patchPalette(skyDay, p.g.cellM); pal[cUrban] != pal[cLow+1] {
		t.Errorf("approach palette by day: city lights %#x, want the ground's %#x", pal[cUrban], pal[cLow+1])
	}
	if pal := patchPalette(skyDay, 200); pal[cSnow] != terrainPal[cSnow] || pal[cUrban] != terrainPal[cUrban] {
		t.Errorf("200 m cells: entry %d is %#x, want snow", cSnow, pal[cSnow])
	}
	// Above approach scale, runways are city-light cells; past runwayMaxCellM, not drawn.
	if q := buildPatch(ctx, flatDEM(180), 36.1245, -86.6782, 180+3000, 0); q.g.cellM <= runwayMaxCellM ||
		q.colour[64*tn+64] == cUrban {
		t.Errorf("%d m cells: runway drawn (centre cell %d)", q.g.cellM, q.colour[64*tn+64])
	}
	q := buildPatch(ctx, flatDEM(180), 36.1245, -86.6782, 180+600, 0)
	if r := runways[0]; q.g.cellM <= approachCellM || r.airport == "" {
		t.Fatalf("1,500 m above: %d m cells", q.g.cellM)
	}
	for _, r := range runways {
		if r.airport == "KBNA" {
			x, y := q.g.cellXY((r.lat1+r.lat2)/2, (r.lon1+r.lon2)/2)
			found := false // half a cell either side: a slanted centreline may miss its own cell's centre
			for dy := -1; dy <= 1; dy++ {
				for dx := -1; dx <= 1; dx++ {
					found = found || q.colour[(int(y)+dy)*tn+int(x)+dx] == cUrban
				}
			}
			if !found {
				t.Errorf("120 m cells: no runway cell around %s's midpoint", r.le)
			}
		}
	}
	// four runways, ~11,000 ft at most, 60 m cells, 2-3 cells wide: several hundred cells, not a slab
	if urban < 300 || urban > 1500 {
		t.Errorf("%d runway cells", urban)
	}
	if c := p.colour[5*tn+5]; c == cUrban {
		t.Errorf("a corner cell, km from the runways, is runway")
	}
}

// A fix's age counts from now, not from the cached snapshot's time: a snapshot 1.2 s old with seen_pos 2 is a
// fix ~3 s old (it came up short before, pulling the camera back every few fixes).
func TestTrackAgeFromSnapshot(t *testing.T) {
	g := geom{lat: 36.1, lon: -86.7, cellM: 60, levelCm: 375, baseM: 150, hiM: 300}
	la, lo := g.latLon(64, 64)
	seen, alt := 2.0, 3000.0
	src := newSource("http://upstream.invalid", time.Second)
	src.last = &upstreamResp{Now: float64(time.Now().Add(-1200 * time.Millisecond).UnixMilli()),
		AC: []upstreamAC{{Hex: "abcdef", Lat: &la, Lon: &lo, AltBaro: alt, SeenPos: &seen}}}
	src.lastAt = time.Now()
	rec := httptest.NewRecorder()
	trackHandler(src, flatDEM(150), newTurns(), nil).ServeHTTP(rec, httptest.NewRequest("GET",
		trackPath+"?icao=abcdef&p="+g.key()+"&n=0&v="+strconv.Itoa(frameVer), nil))
	b := rec.Body.Bytes()
	if len(b) != trackSize {
		t.Fatalf("%d: %q", rec.Code, b)
	}
	if b[25] != 3 {
		t.Errorf("age %d s, want 3", b[25])
	}
}

func btoi(b bool) int {
	if b {
		return 1
	}
	return 0
}
