package main

import (
	"os"
	"strings"
	"testing"
	"time"
)

func lines(n notice) [3]string {
	return [3]string{strings.TrimRight(string(n.lines[0][:]), " "), strings.TrimRight(string(n.lines[1][:]), " "),
		strings.TrimRight(string(n.lines[2][:]), " ")}
}

func TestComposeNotice(t *testing.T) {
	cases := []struct {
		f    atcFlight
		want [3]string
	}{
		{atcFlight{UUID: "a", Number: "SWA1234", Airline: "Southwest Airlines", Type: "B738", Origin: "BNA",
			Destination: "MDW", AltFt: 1800, GsKt: 160},
			[3]string{"* SWA1234", "SOUTHWEST   B738", "BNA > MDW  1,800FT 160KT"}},
		{atcFlight{UUID: "b", Number: "DAL88", Airline: "Delta Air Lines", Type: "Boeing 737-900ER",
			Origin: "Unknown", Destination: "ATL", AltFt: 35000, GsKt: 480},
			[3]string{"* DAL88", "DELTA       B737-900ER", "TO ATL     35,000FT 480KT"}},
		{atcFlight{UUID: "c", Number: "AAL9", Airline: "Unknown", Type: "Airbus A321neo", Origin: "Unknown",
			Destination: "Unknown", AltFt: 2500},
			[3]string{"* AAL9", "AMERICAN    A321NEO", "2,500FT"}},
		{atcFlight{UUID: "d", Number: "Unknown", Airline: "Air Canada", Type: "Unknown"},
			[3]string{"* MYSTERY PLANE", "AIR CANADA", ""}},
		{atcFlight{UUID: "e", Number: "N123AB", Airline: "Unknown", Type: "Cessna 172", Origin: "Nashville International Airport",
			Destination: "Chicago Midway International", AltFt: 12500, GsKt: 110},
			[3]string{"* N123AB", "C172", "NASHVILLE > CHICAGO"}},
	}
	for _, c := range cases {
		got := lines(composeNotice(c.f, nil))
		head := ""
		for _, h := range noticeHeadlines {
			if strings.HasPrefix(got[0], h) {
				head = h
			}
		}
		if head == "" {
			t.Errorf("%s: headline %q", c.f.Number, head)
		}
		got[0] = "* " + strings.TrimSpace(got[0][len(head):])
		if got != c.want {
			t.Errorf("%s:\n got %q\nwant %q", c.f.Number, got, c.want)
		}
		for _, l := range got {
			if len(l) > noticeCols {
				t.Errorf("%q is longer than %d", l, noticeCols)
			}
		}
	}
}

func TestComposeNoticeMatched(t *testing.T) {
	alt := 1700.0
	ac := &upstreamAC{Hex: "a1b2c3", Flight: "SWA1234 ", Squawk: "7700", AltBaro: alt, Lat: ptr(36.1), Lon: ptr(-86.6), Type: "B38M"}
	n := composeNotice(atcFlight{UUID: "x", Number: "WN1234", Type: "Unknown", AltFt: 1800}, ac)
	l := lines(n)
	if n.flags != nICAO|nEmerg || n.icao != 0xa1b2c3 {
		t.Fatalf("flags %#x icao %06x", n.flags, n.icao)
	}
	if l[0] != "MAYDAY!    SWA1234" || l[1] != "SOUTHWEST   B38M" {
		t.Fatalf("got %q", l)
	}
}

func ptr(v float64) *float64 { return &v }

func TestNoticeRing(t *testing.T) {
	now := time.Date(2026, 11, 1, 12, 0, 0, 0, time.UTC)
	ns := newNotices(nil, 30*time.Second)
	ns.now = func() time.Time { return now }
	ns.next = 0xFFFE // wraps past 0, which is never an id
	if _, ok := ns.after(0); ok {
		t.Fatal("empty ring has a notice")
	}
	var ids []uint16
	for _, u := range []string{"a", "b", "a", "c"} { // "a" again: a QoS 1 redelivery
		if n, ok := ns.add(atcFlight{UUID: u, Number: "X" + u, Timestamp: now.Format(time.RFC3339)}); ok {
			ids = append(ids, n.id)
		}
	}
	if len(ids) != 3 || ids[0] != 0xFFFF || ids[1] != 1 || ids[2] != 2 {
		t.Fatalf("ids %x", ids)
	}
	if n, _ := ns.after(0); n.id != 0xFFFF {
		t.Fatalf("after 0: %x", n.id)
	}
	if n, _ := ns.after(0xFFFF); n.id != 1 {
		t.Fatalf("after ffff: %x", n.id)
	}
	if _, ok := ns.after(2); ok {
		t.Fatal("nothing is after the newest")
	}
	if _, ok := ns.add(atcFlight{UUID: "old", Timestamp: now.Add(-2 * time.Minute).Format(time.RFC3339)}); ok {
		t.Fatal("a 2-minute-old report should be dropped")
	}
	for i := 0; i < noticeKeep+2; i++ {
		ns.add(atcFlight{UUID: string(rune('k' + i))})
	}
	if len(ns.ring) != noticeKeep {
		t.Fatalf("ring holds %d", len(ns.ring))
	}
	now = now.Add(31 * time.Second)
	if _, ok := ns.after(0); ok {
		t.Fatal("notices outlive the TTL")
	}
}

func TestNoticeMatch(t *testing.T) {
	now := time.Date(2026, 11, 1, 12, 0, 0, 0, time.UTC)
	s := newSource("", time.Second)
	s.last = &upstreamResp{Now: float64(now.UnixMilli()), AC: []upstreamAC{
		{Hex: "aaaaaa", Flight: "SWA1234", Lat: ptr(36.20), Lon: ptr(-86.60), AltBaro: 3000.0},
		// 1.2 nm north of the FR24 fix, at 160 kt 30 s on: where dead reckoning puts it
		{Hex: "bbbbbb", Flight: "N555", Lat: ptr(36.1300 + 1.333/60), Lon: ptr(-86.6750), AltBaro: 1900.0},
		{Hex: "cccccc", Flight: "N666", Lat: ptr(36.1300 + 0.2/60), Lon: ptr(-86.6750), AltBaro: 9000.0},
	}}
	s.lastAt = now
	fix := atcFlight{Lat: 36.1300, Lon: -86.6750, Heading: 0, GsKt: 160, AltFt: 1800,
		Timestamp: now.Add(-30 * time.Second).Format(time.RFC3339)}

	f := fix
	f.Number = "SWA1234"
	if a := s.match(f, now); a == nil || a.Hex != "aaaaaa" {
		t.Fatalf("callsign match: %+v", a)
	}
	f.Number = "WN9"
	if a := s.match(f, now); a == nil || a.Hex != "bbbbbb" {
		t.Fatalf("position match (projected, cccccc is nearer the raw fix but 7,000 ft off): %+v", a)
	}
	if a := s.match(f, now.Add(20*time.Second)); a != nil {
		t.Fatalf("a 20 s old snapshot matched: %+v", a)
	}
	f.Lat, f.Lon = 37, -88
	if a := s.match(f, now); a != nil {
		t.Fatalf("far away matched: %+v", a)
	}
}

// The sample dev message (scripts/mqtt pub) composes and fits.
func TestNoticeSample(t *testing.T) {
	b, err := os.ReadFile("testdata/atc/entering.json")
	if err != nil {
		t.Fatal(err)
	}
	fs, err := parseATC(b)
	if err != nil || len(fs) != 1 {
		t.Fatalf("%v %v", fs, err)
	}
	if l := lines(composeNotice(fs[0], nil)); l[1] != "SOUTHWEST   B738" || l[2] != "BNA > MDW  1,800FT 160KT" {
		t.Fatalf("%q", l)
	}
}

func TestThousands(t *testing.T) {
	for v, want := range map[int]string{0: "0", 999: "999", 1000: "1,000", 35000: "35,000", 1234567: "1,234,567"} {
		if got := thousands(v); got != want {
			t.Errorf("%d: %q", v, got)
		}
	}
}
