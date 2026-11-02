package main

import (
	"encoding/binary"
	"fmt"
	"math"
	"net/http/httptest"
	"testing"
)

const bnaLat, bnaLon = 36.1245, -86.6782

func TestMapRunways(t *testing.T) {
	n := 0
	for _, r := range runways {
		if r.airport == "KBNA" {
			n++
		}
	}
	if n != 4 {
		t.Fatalf("KBNA has %d runways in runways.csv, want 4", n)
	}
}

func TestMapBuild(t *testing.T) {
	inDisc := func(m vmap) {
		for _, s := range m.segs {
			for _, p := range [][2]uint8{{s.x1, s.y1}, {s.x2, s.y2}} {
				if d := math.Hypot(float64(p[0])-mapScope, float64(p[1])-mapScope); d > mapClipR+1 {
					t.Errorf("range %d: segment end %v is %.1f px from the centre", m.rangeNM, p, d)
				}
			}
		}
	}
	// 10 nm: 4 runways, 8 centrelines, 8 x 10 ticks less the far ones off the disc (the airport is off-centre).
	m := buildMap(bnaLat, bnaLon, 10)
	inDisc(m)
	if len(m.segs) < 4+8+64 || len(m.segs) > 4+8+80 || m.flags != 0 || m.n != [3]int{4, 8, len(m.segs) - 12} {
		t.Errorf("10 nm: %d segments (flags %#x), want 76-92", len(m.segs), m.flags)
	}
	// A centreline runs 10 nm out: at 10 nm range from the airport near the centre, it nearly reaches the rim.
	far := 0.0
	for _, s := range m.segs[4:12] {
		far = math.Max(far, math.Max(math.Hypot(float64(s.x1)-mapScope, float64(s.y1)-mapScope),
			math.Hypot(float64(s.x2)-mapScope, float64(s.y2)-mapScope)))
	}
	if far < 90 {
		t.Errorf("10 nm: centrelines reach %.0f px, want the rim", far)
	}
	// 25 nm: ticks every 5 nm; 50 nm and wider: runways only.
	if m := buildMap(bnaLat, bnaLon, 25); len(m.segs) != 4+8+16 {
		t.Errorf("25 nm: %d segments, want 28", len(m.segs))
	}
	for _, r := range []int{50, 100} {
		m := buildMap(bnaLat, bnaLon, r)
		inDisc(m)
		if len(m.segs) < 4 {
			t.Errorf("%d nm: %d segments, want KBNA's runways at least", r, len(m.segs))
		}
	}
	// 5 nm: everything clipped to the disc, under the cap.
	if m := buildMap(bnaLat, bnaLon, 5); len(m.segs) > mapMax || len(m.segs) < 12 {
		t.Errorf("5 nm: %d segments", len(m.segs))
	} else {
		inDisc(m)
	}
	// Mid-ocean: nothing.
	if m := buildMap(30, -40, 25); len(m.segs) != 0 || m.flags != mNone {
		t.Errorf("mid-Atlantic: %d segments, flags %#x", len(m.segs), m.flags)
	}
}

func TestMapClip(t *testing.T) {
	a, b, ok := clipCircle(pt{104, 104}, pt{400, 104})
	if !ok || a.x != 104 || math.Abs(b.x-104-mapClipR) > 1e-9 {
		t.Errorf("clip: %v %v %v", a, b, ok)
	}
	if _, _, ok := clipCircle(pt{0, 0}, pt{10, 0}); ok {
		t.Error("a segment off the disc survives clipping")
	}
}

func TestMapHandler(t *testing.T) {
	rec := httptest.NewRecorder()
	mapHandler().ServeHTTP(rec, httptest.NewRequest("GET", fmt.Sprintf("%s?lat=%v&lon=%v&range=10&v=%d", mapPath,
		bnaLat, bnaLon, frameVer), nil))
	b := rec.Body.Bytes()
	if rec.Code != 200 || len(b) < mapHdr || string(b[:2]) != "AM" || b[2] != frameVer ||
		len(b) != mapHdr+4*(int(b[6])+int(b[7])+int(b[8])) || binary.BigEndian.Uint16(b[4:]) != 10 ||
		b[6] != 4 || b[7] != 8 {
		t.Errorf("status %d, % x", rec.Code, b[:min(len(b), 16)])
	}
}
