package main

import (
	"compress/gzip"
	"context"
	"encoding/json"
	"math"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// -record writes what the upstream answers, one line each; -replay serves it back as an upstream: the
// snapshot for the time since start, looping, "now" on the wall clock, and single aircraft by hex.
func TestRecordReplay(t *testing.T) {
	n := 0
	up := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		n++
		json.NewEncoder(w).Encode(map[string]any{"now": 1_000_000 + n*1000, "ac": []map[string]any{
			{"hex": "abc123", "alt_baro": 1000 * n}, {"hex": "~def456", "alt_baro": 5}}})
	}))
	defer up.Close()
	path := filepath.Join(t.TempDir(), "rec.jsonl.gz")
	if err := recordUpstream(context.Background(), path, up.URL, 36.1, -86.7, 12, 10*time.Millisecond,
		45*time.Millisecond); err != nil {
		t.Fatal(err)
	}
	snaps, err := loadRecording(path)
	if err != nil || len(snaps) < 3 {
		t.Fatalf("%d snapshots, %v", len(snaps), err)
	}
	if snaps[1].now-snaps[0].now != 1000 || snaps[0].hex[1] != "def456" {
		t.Fatalf("snapshots %+v", snaps[:2])
	}

	rp := newReplay(snaps[:3], 0)
	clock := rp.start
	rp.clock = func() time.Time { return clock }
	srv := httptest.NewServer(rp.handler())
	defer srv.Close()
	get := func(path string) upstreamResp {
		var u upstreamResp
		resp, err := http.Get(srv.URL + path)
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		json.NewDecoder(resp.Body).Decode(&u)
		return u
	}
	alt := func(u upstreamResp) float64 { f, _ := u.AC[0].AltBaro.(float64); return f }
	area := "/api/v3/lat/36.1/lon/-86.7/dist/25"
	first := alt(get(area))
	clock = clock.Add(1500 * time.Millisecond)
	if u := get(area); alt(u) != first+1000 || u.Now != float64(clock.UnixMilli()) {
		t.Errorf("1.5 s in: alt %v (first %v), now %v", alt(u), first, u.Now)
	}
	clock = clock.Add(1500 * time.Millisecond) // 3 s: past the last snapshot's second, back to the first
	if u := get(area); alt(u) != first {
		t.Errorf("3 s in: alt %v, want the first snapshot's %v", alt(u), first)
	}
	if u := get("/api/v2/hex/DEF456"); len(u.AC) != 1 || u.AC[0].Hex != "~def456" {
		t.Errorf("hex lookup: %+v", u.AC)
	}
	if u := get("/api/v2/hex/000000"); len(u.AC) != 0 {
		t.Errorf("unknown hex: %+v", u.AC)
	}
}

func TestLoadRecordingErrors(t *testing.T) {
	dir := t.TempDir()
	if _, err := loadRecording(filepath.Join(dir, "missing")); err == nil {
		t.Error("missing file accepted")
	}
	empty := filepath.Join(dir, "empty.gz")
	f, _ := os.Create(empty)
	gzip.NewWriter(f).Close()
	f.Close()
	if _, err := loadRecording(empty); err == nil {
		t.Error("empty recording accepted")
	}
}

// Real arrivals (testdata/arrivals): with the geoid off GPS altitude, SCX3003's last airborne fix is at KBNA's
// field elevation (183 m), where the raw ellipsoid height is 30 m under it; and no fix of either arrival puts
// the camera below eye height over the ground, though low fixes are quantised and noisy (SWA3092 reports
// 425 ft over a 20L that is higher).
func TestArrivalRecordings(t *testing.T) {
	ctx := context.Background()
	const fieldM = 183.0
	for _, c := range []struct{ file, hex string }{
		{"testdata/arrivals/kbna-20l-swa3092-b38m.jsonl.gz", "ac3751"},
		{"testdata/arrivals/kbna-20r-scx3003-b738.jsonl.gz", "aac5ec"},
	} {
		ups, _, err := readUpstreams([]string{c.file})
		if err != nil {
			t.Fatal(err)
		}
		g := buildPatch(ctx, flatDEM(fieldM), 36.1245, -86.6782, math.NaN(), 0).g
		var last *upstreamAC
		ground := false
		for i := range ups {
			for j := range ups[i].AC {
				a := &ups[i].AC[j]
				if a.Hex != c.hex || a.Lat == nil {
					continue
				}
				k := buildTrack(ctx, a, g, flatDEM(fieldM), newTurns(), c.hex, time.UnixMilli(int64(ups[i].Now)))
				if m := float64(g.baseM) + float64(k.z)/256*float64(g.levelCm)/100; m < fieldM+eyeM-0.5 {
					t.Fatalf("%s: camera at %.1f m, under eye height over %.0f m ground", c.file, m, fieldM)
				}
				if _, gnd := altMetres(a); gnd {
					ground = true
				} else if !ground && a.AltGeom != nil {
					last = a
				}
			}
		}
		if !ground || last == nil {
			t.Fatalf("%s: no landing in the recording", c.file)
		}
		if c.hex == "aac5ec" {
			if m, _ := altMetres(last); math.Abs(m-fieldM) > 15 || math.Abs(*last.AltGeom*0.3048-fieldM) < 25 {
				t.Errorf("SCX3003's last airborne fix: %.0f m (raw %.0f m), field %.0f m", m, *last.AltGeom*0.3048, fieldM)
			}
		}
	}
}
