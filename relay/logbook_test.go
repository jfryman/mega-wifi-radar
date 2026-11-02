package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func testLogbook(t *testing.T, ns *notices) *logbook {
	t.Helper()
	lb, err := openLogbook(fmt.Sprintf("file:%s?mode=memory&cache=shared", t.Name()), newSource("http://upstream.invalid", time.Second), ns)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { lb.db.Close() })
	return lb
}

func loadSnapshots(t *testing.T) []*upstreamResp {
	t.Helper()
	fs, _ := filepath.Glob("testdata/kbna-seq/*.json")
	var ups []*upstreamResp
	for _, f := range fs {
		data, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		var up upstreamResp
		if err := json.Unmarshal(data, &up); err != nil {
			t.Fatal(err)
		}
		ups = append(ups, &up)
	}
	if len(ups) == 0 {
		t.Fatal("no snapshots")
	}
	return ups
}

func TestHomeZone(t *testing.T) {
	for _, c := range []struct {
		lat, lon float64
		icao, tz string
	}{{36.1245, -86.6782, "KBNA", "America/Chicago"}, {51.4707, -0.4599, "EGLL", "Europe/London"},
		{33.4353, -112.0059, "KPHX", "America/Phoenix"}} {
		icao, loc := homeZone(c.lat, c.lon)
		if icao != c.icao || loc.String() != c.tz {
			t.Errorf("%v,%v: %s %s, want %s %s", c.lat, c.lon, icao, loc, c.icao, c.tz)
		}
	}
}

func TestLogbookReplay(t *testing.T) {
	ns := newNotices(nil, time.Minute)
	lb := testLogbook(t, ns)
	ups := loadSnapshots(t)
	now := time.UnixMilli(int64(ups[0].Now))
	lb.now = func() time.Time { return now }
	h := lb.seen(36.1245, -86.6782)

	// One aircraft is made military and one LADD: the first is rare (and announced once), the second never logged.
	var milHex, laddHex string
	for _, up := range ups {
		for i := range up.AC {
			a := &up.AC[i]
			if milHex == "" && a.Lat != nil {
				milHex = a.Hex
			} else if laddHex == "" && a.Lat != nil && a.Hex != milHex {
				laddHex = a.Hex
			}
			if a.Hex == milHex {
				a.DBFlags = dbMil
			}
			if a.Hex == laddHex {
				a.DBFlags = dbLADD
			}
		}
	}
	for _, up := range ups {
		now = time.UnixMilli(int64(up.Now))
		lb.record(h, up)
	}
	s, err := lb.summary(36.1245, -86.6782)
	if err != nil {
		t.Fatal(err)
	}
	unique, mil := map[string]bool{}, map[string]bool{}
	for _, up := range ups {
		for _, a := range up.AC {
			if a.Lat != nil && a.Hex != laddHex {
				if d, _ := distBearingNM(36.1245, -86.6782, *a.Lat, *a.Lon); d <= logRangeNM {
					unique[a.Hex] = true
					if a.DBFlags&dbMil != 0 {
						mil[a.Hex] = true
					}
				}
			}
		}
	}
	if s.aircraft != len(unique) || s.aircraft == 0 || s.types == 0 {
		t.Errorf("%d aircraft (%d types), want %d", s.aircraft, s.types, len(unique))
	}
	if s.recs[0].value == 0 || s.recs[1].value == 0 || s.recs[0].cs == "" {
		t.Errorf("records %+v", s.recs)
	}
	if len(s.rares) != len(mil) || len(mil) < 2 || s.rares[0].rare&rMil == 0 {
		t.Errorf("rares %+v, want the %d military aircraft (the recording's own + ours)", s.rares, len(mil))
	}
	n, ok := ns.after(0)
	if !ok || !strings.HasPrefix(string(n.lines[0][:]), "MILITARY!") || len(ns.ring) != len(mil) {
		t.Errorf("rare notices: %v %q (ring %d, want one each)", ok, n.lines[0][:], len(ns.ring))
	}
	b := s.encode()
	if len(b) != logSize || string(b[:2]) != "AL" || int(binary.BigEndian.Uint16(b[6:])) != s.aircraft || int(b[12]) != len(mil) {
		t.Errorf("encoded % x", b[:16])
	}
}

func TestLogbookDay(t *testing.T) {
	lb := testLogbook(t, nil)
	loc, _ := time.LoadLocation("America/Chicago")
	now := time.Date(2026, 11, 2, 23, 59, 0, 0, loc)
	lb.now = func() time.Time { return now }
	h := lb.seen(36.1245, -86.6782)
	lat, lon, gs := 36.2, -86.7, 300.0
	lb.record(h, &upstreamResp{AC: []upstreamAC{{Hex: "abc123", Flight: "SWA1", Lat: &lat, Lon: &lon, GS: &gs, AltBaro: 9000.0}}})
	if s, _ := lb.summary(36.1245, -86.6782); s.aircraft != 1 || s.mday != 2 {
		t.Errorf("before midnight: %d aircraft on the %d", s.aircraft, s.mday)
	}
	now = now.Add(2 * time.Minute) // past local midnight (05:01 UTC): a new day
	if s, _ := lb.summary(36.1245, -86.6782); s.aircraft != 0 || s.mday != 3 || s.flags&lNew == 0 {
		t.Errorf("after local midnight: %d aircraft on the %d, flags %#x", s.aircraft, s.mday, s.flags)
	}
}

func TestLogbookPersists(t *testing.T) {
	path := filepath.Join(t.TempDir(), "relay.db")
	lb, err := openLogbook(path, newSource("http://upstream.invalid", time.Second), nil)
	if err != nil {
		t.Fatal(err)
	}
	lb.seen(36.1245, -86.6782)
	lb.db.Close()
	lb, err = openLogbook(path, newSource("http://upstream.invalid", time.Second), nil)
	if err != nil {
		t.Fatal(err)
	}
	defer lb.db.Close()
	if len(lb.homes) != 1 {
		t.Errorf("reopened: %d homes, want the one a cart asked about", len(lb.homes))
	}
}

func TestLogHandler(t *testing.T) {
	lb := testLogbook(t, nil)
	rec := httptest.NewRecorder()
	logHandler(lb).ServeHTTP(rec, httptest.NewRequest("GET", fmt.Sprintf("%s?lat=36.1245&lon=-86.6782&v=%d", logPath, frameVer), nil))
	if b := rec.Body.Bytes(); rec.Code != 200 || len(b) != logSize || string(b[:2]) != "AL" || b[3]&lNew == 0 {
		t.Errorf("status %d, % x", rec.Code, b[:min(len(b), 16)])
	}
}
