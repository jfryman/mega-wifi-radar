package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"
)

const kbnaLat, kbnaLon = 36.1245, -86.6782

func f64(v float64) *float64 { return &v }

// acAt places an aircraft dx nm east / dy nm north of KBNA (small-offset approximation).
func acAt(hex string, dxNM, dyNM float64) upstreamAC {
	lat := kbnaLat + dyNM/60
	lon := kbnaLon + dxNM/(60*cosDeg(kbnaLat))
	return upstreamAC{Hex: hex, Lat: &lat, Lon: &lon, SeenPos: f64(0.5)}
}

func cosDeg(d float64) float64 { return math.Cos(d * math.Pi / 180) }

func TestConvertFields(t *testing.T) {
	a := acAt("a46745", 10, 0)
	a.Flight, a.AltBaro, a.GS, a.Track = "CTE615  ", 21000.0, f64(278.2), f64(126.38)
	a.BaroRate, a.Squawk, a.Category, a.Emergency = f64(-1280), "1717", "A1", "none"
	r, ok := convert(&a, kbnaLat, kbnaLon, 25)
	if !ok {
		t.Fatal("dropped")
	}
	if r.icao != 0xa46745 || r.alt != 840 || r.gs != 139 || r.track != 90 || r.vrate != -20 ||
		r.squawk != 0x1717 || r.cat != 0x01 || string(r.cs[:]) != "CTE615  " || r.flags != 0 {
		t.Fatalf("bad record %+v", r)
	}
	if r.x < 318 || r.x > 322 || r.y < -2 || r.y > 2 { // 10 nm east = 320
		t.Fatalf("projection x=%d y=%d, want ~320,0", r.x, r.y)
	}
}

func TestConvertRules(t *testing.T) {
	cases := []struct {
		name  string
		mod   func(*upstreamAC)
		check func(record, bool) string
	}{
		{"ground", func(a *upstreamAC) { a.AltBaro = "ground" }, flagIs(fGround | fGSUnk | fTrkUnk | fVRUnk)},
		{"missing everything", func(a *upstreamAC) {}, flagIs(fAltUnk | fGSUnk | fTrkUnk | fVRUnk)},
		{"squawk 7700", func(a *upstreamAC) { a.Squawk = "7700"; a.AltBaro = 1000.0 }, hasFlag(fEmerg)},
		{"emergency field", func(a *upstreamAC) { a.Emergency = "minfuel"; a.AltBaro = 1000.0 }, hasFlag(fEmerg)},
		{"stale", func(a *upstreamAC) { a.SeenPos = f64(20) }, hasFlag(fStale)},
		{"dropped >60s", func(a *upstreamAC) { a.SeenPos = f64(61) }, dropped},
		{"no position", func(a *upstreamAC) { a.Lat = nil }, dropped},
		{"out of range", func(a *upstreamAC) { lat := kbnaLat + 30.0/60; a.Lat = &lat }, dropped},
		{"track wraps", func(a *upstreamAC) { a.Track = f64(359.9) }, func(r record, _ bool) string {
			return want(r.track == 0, "track %d, want 0", r.track)
		}},
		{"gs saturates", func(a *upstreamAC) { a.GS = f64(600) }, func(r record, _ bool) string {
			return want(r.gs == 255, "gs %d, want 255", r.gs)
		}},
		{"vrate saturates", func(a *upstreamAC) { a.BaroRate = f64(12000) }, func(r record, _ bool) string {
			return want(r.vrate == 127, "vrate %d, want 127", r.vrate)
		}},
		{"geom_rate fallback", func(a *upstreamAC) { a.GeomRate = f64(640) }, func(r record, _ bool) string {
			return want(r.vrate == 10 && r.flags&fVRUnk == 0, "vrate %d flags %#x", r.vrate, r.flags)
		}},
		{"callsign from reg", func(a *upstreamAC) { a.Reg = "n383jp" }, csIs("N383JP  ")},
		{"callsign from hex", func(a *upstreamAC) { a.Hex = "~abc123" }, csIs("ABC123  ")},
		{"callsign truncated", func(a *upstreamAC) { a.Flight = "ABCDEFGHIJ" }, csIs("ABCDEFGH")},
		{"category B1", func(a *upstreamAC) { a.Category = "B1" }, func(r record, _ bool) string {
			return want(r.cat == 0x11, "cat %#x", r.cat)
		}},
		{"no squawk/cat", func(a *upstreamAC) {}, func(r record, _ bool) string {
			return want(r.squawk == 0xFFFF && r.cat == 0xFF, "squawk %#x cat %#x", r.squawk, r.cat)
		}},
	}
	for _, c := range cases {
		a := acAt("a00001", 3, 4)
		c.mod(&a)
		r, ok := convert(&a, kbnaLat, kbnaLon, 25)
		if msg := c.check(r, ok); msg != "" {
			t.Errorf("%s: %s", c.name, msg)
		}
	}
}

func want(ok bool, f string, a ...any) string {
	if ok {
		return ""
	}
	return fmt.Sprintf(f, a...)
}
func flagIs(f uint8) func(record, bool) string {
	return func(r record, ok bool) string { return want(ok && r.flags == f, "flags %#x, want %#x", r.flags, f) }
}
func hasFlag(f uint8) func(record, bool) string {
	return func(r record, ok bool) string { return want(ok && r.flags&f != 0, "flags %#x lack %#x", r.flags, f) }
}
func csIs(s string) func(record, bool) string {
	return func(r record, _ bool) string { return want(string(r.cs[:]) == s, "cs %q, want %q", r.cs[:], s) }
}
func dropped(_ record, ok bool) string { return want(!ok, "kept, want dropped") }

func TestEncodeLayout(t *testing.T) {
	f := frame{seq: 0x1234, time: 0x01020304, rangeNM: 25, source: 1, hflags: hTruncated,
		recs: []record{{icao: 0xa46745, flags: fStale, x: -2, y: 320, alt: 840, squawk: 0x7700,
			gs: 139, track: 90, vrate: -20, cat: 0x11, cs: [8]byte{'U', 'A', 'L', '1', ' ', ' ', ' ', ' '}}}}
	b := f.encode()
	wantHex := "4152010100181234010203040019010" + "1" +
		"a46745" + "04" + "fffe" + "0140" + "0348" + "7700" + "8b" + "5a" + "ec" + "11" + "55414c3120202020"
	if got := fmt.Sprintf("%x", b); got != wantHex {
		t.Fatalf("encoding\n got %s\nwant %s", got, wantHex)
	}
}

func TestKBNASnapshot(t *testing.T) {
	data, err := os.ReadFile("testdata/kbna-25nm.json")
	if err != nil {
		t.Fatal(err)
	}
	var up upstreamResp
	if err := json.Unmarshal(data, &up); err != nil {
		t.Fatal(err)
	}
	f := buildFrame(&up, kbnaLat, kbnaLon, 25, maxRecs, 7)
	b := f.encode()
	if len(f.recs) == 0 || len(b) != hdrSize+recSize*len(f.recs) || b[3] != byte(len(f.recs)) {
		t.Fatalf("%d recs, %d bytes", len(f.recs), len(b))
	}
	for i := 1; i < len(f.recs); i++ {
		if f.recs[i].distNM < f.recs[i-1].distNM {
			t.Fatalf("not sorted nearest-first at %d", i)
		}
	}
	capped := buildFrame(&up, kbnaLat, kbnaLon, 25, 5, 7)
	if len(capped.recs) != 5 || capped.hflags&hTruncated == 0 {
		t.Fatalf("cap: %d recs, hflags %#x", len(capped.recs), capped.hflags)
	}
}

func TestHTTP(t *testing.T) {
	// Fake upstream with 96 aircraft so the frame (2320 B) exceeds net/http's 2 KiB buffer.
	var acs []upstreamAC
	for i := 0; i < 120; i++ {
		acs = append(acs, acAt(fmt.Sprintf("%06x", 0xa00000+i), float64(i%20)-10, float64(i/20)-3))
	}
	calls := 0
	up := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls++
		if !strings.HasPrefix(r.URL.Path, "/api/v3/lat/36.1245/lon/-86.6782/dist/") ||
			r.Header.Get("User-Agent") != userAgent {
			t.Errorf("upstream request %s UA %q", r.URL.Path, r.Header.Get("User-Agent"))
		}
		json.NewEncoder(w).Encode(upstreamResp{Now: 1.7e12, AC: acs})
	}))
	defer up.Close()
	src := newSource(up.URL, 50*time.Millisecond)
	relay := httptest.NewServer(frameHandler(src))
	defer relay.Close()

	resp, err := http.Get(relay.URL + "/v1/frame?lat=36.1245&lon=-86.6782&range=25&max=200")
	if err != nil {
		t.Fatal(err)
	}
	body, _ := io.ReadAll(resp.Body)
	resp.Body.Close()
	if resp.StatusCode != 200 || resp.ContentLength != 2320 || len(body) != 2320 ||
		len(resp.TransferEncoding) != 0 || resp.Header.Get("Content-Type") != "application/octet-stream" {
		t.Fatalf("status %d len %d body %d te %v", resp.StatusCode, resp.ContentLength, len(body), resp.TransferEncoding)
	}
	if body[3] != maxRecs || body[15]&hTruncated == 0 || binary.BigEndian.Uint16(body[6:]) != 1 {
		t.Fatalf("header % x", body[:16])
	}
	// A repeat inside min-interval is served from cache.
	http.Get(relay.URL + "/v1/frame?lat=36.1245&lon=-86.6782&range=25&max=200")
	if calls != 1 {
		t.Fatalf("%d upstream calls, want 1", calls)
	}

	resp, _ = http.Get(relay.URL + "/v1/frame?lon=1")
	if resp.StatusCode != http.StatusBadRequest {
		t.Fatalf("missing lat: status %d", resp.StatusCode)
	}

	// Upstream down: serve the last good frame flagged UPSTREAM_ERR.
	up.Close()
	time.Sleep(60 * time.Millisecond)
	resp, err = http.Get(relay.URL + "/v1/frame?lat=36.1245&lon=-86.6782&range=25&max=200")
	if err != nil {
		t.Fatal(err)
	}
	body, _ = io.ReadAll(resp.Body)
	if resp.StatusCode != 200 || body[15]&hUpstreamErr == 0 {
		t.Fatalf("upstream down: status %d hflags %#x", resp.StatusCode, body[15])
	}
}
