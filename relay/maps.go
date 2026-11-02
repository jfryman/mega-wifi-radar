package main

// Video maps (lode/ui/video-maps.md): runways around home and the home airport's extended centrelines, as line
// segments in scope pixels at the cart's range, so the cart only draws lines. Runways come from OurAirports
// (relay/runways.csv, tools/runwaygen): the cart's home airports.

import (
	_ "embed"
	"encoding/binary"
	"fmt"
	"log"
	"math"
	"net/http"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"
)

const (
	mapPath   = "/v1/map"
	mapMax    = 160 // AR_MAP_MAX
	mapHdr    = 8   // AR_MAP_HDR
	mNone     = 0x01
	mapScope  = 104 // the scope's centre and radius in pixels
	mapClipR  = 103 // segments are clipped to this radius
	mapHomeNM = 5   // the home airport: the nearest within this of the home point

	mapCentreNM       = 10 // extended centrelines run this far from each threshold
	mapCentreMaxRange = 25 // ... at ranges up to this; wider ranges show runways only
	mapTickPx         = 2  // half a centreline tick
)

//go:embed runways.csv
var runwaysCSV string

type runway struct {
	airport    string
	le, he     string
	lat1, lon1 float64 // the le end
	lat2, lon2 float64 // the he end
}

var runways = parseRunways(runwaysCSV)

func parseRunways(s string) []runway {
	var rs []runway
	for _, line := range strings.Split(s, "\n") {
		f := strings.Split(line, ",")
		if len(f) != 7 || strings.HasPrefix(line, "#") {
			continue
		}
		var v [4]float64
		ok := true
		for i, k := range []int{2, 3, 5, 6} {
			var err error
			if v[i], err = strconv.ParseFloat(f[k], 64); err != nil {
				ok = false
			}
		}
		if ok {
			rs = append(rs, runway{f[0], f[1], f[4], v[0], v[1], v[2], v[3]})
		}
	}
	return rs
}

type seg struct{ x1, y1, x2, y2 uint8 }

type vmap struct {
	flags   uint8
	rangeNM uint16
	segs    []seg
}

func (m *vmap) encode() []byte {
	b := make([]byte, mapHdr, mapHdr+4*len(m.segs))
	copy(b, "AM")
	b[2] = frameVer
	b[3] = m.flags
	binary.BigEndian.PutUint16(b[4:], uint16(len(m.segs)))
	binary.BigEndian.PutUint16(b[6:], m.rangeNM)
	for _, s := range m.segs {
		b = append(b, s.x1, s.y1, s.x2, s.y2)
	}
	return b
}

type pt struct{ x, y float64 }

// nmXY is a position in nm east and north of home (the frame's projection).
func nmXY(homeLat, homeLon, lat, lon float64) pt {
	d, brg := distBearingNM(homeLat, homeLon, lat, lon)
	return pt{d * math.Sin(brg), d * math.Cos(brg)}
}

// clipCircle clips a-b to the scope's disc (radius mapClipR about the centre); ok is false when none of it is inside.
func clipCircle(a, b pt) (pt, pt, bool) {
	c := pt{mapScope, mapScope}
	dx, dy := b.x-a.x, b.y-a.y
	fx, fy := a.x-c.x, a.y-c.y
	qa, qb, qc := dx*dx+dy*dy, 2*(fx*dx+fy*dy), fx*fx+fy*fy-mapClipR*mapClipR
	if qa < 1e-9 {
		return a, b, qc <= 0
	}
	disc := qb*qb - 4*qa*qc
	if disc < 0 {
		return a, b, false
	}
	sq := math.Sqrt(disc)
	t0, t1 := math.Max(0, (-qb-sq)/(2*qa)), math.Min(1, (-qb+sq)/(2*qa))
	if t0 > t1 {
		return a, b, false
	}
	return pt{a.x + t0*dx, a.y + t0*dy}, pt{a.x + t1*dx, a.y + t1*dy}, true
}

func px8(v float64) uint8 { return uint8(min(max(math.Round(v), 0), 2*mapScope-1)) }

// buildMap draws the map for a home point at rangeNM: every known runway in view, then the home airport's
// extended centrelines and their ticks (every mile up to 10 nm range, every 5 nm at 25 nm; none wider).
func buildMap(lat, lon float64, rangeNM int) vmap {
	m := vmap{rangeNM: uint16(rangeNM)}
	scale := mapScope / float64(rangeNM)
	toPx := func(p pt) pt { return pt{mapScope + p.x*scale, mapScope - p.y*scale} }
	add := func(a, b pt, keepDot bool) {
		a, b, ok := clipCircle(a, b)
		if !ok || len(m.segs) >= mapMax {
			return
		}
		s := seg{px8(a.x), px8(a.y), px8(b.x), px8(b.y)}
		if s.x1 == s.x2 && s.y1 == s.y2 && !keepDot {
			return
		}
		m.segs = append(m.segs, s)
	}

	// The home airport: the nearest runway midpoint within mapHomeNM.
	home, best := "", math.Inf(1)
	type rw struct {
		r      *runway
		p1, p2 pt // nm
		dist   float64
	}
	var inView []rw
	for i := range runways {
		r := &runways[i]
		p1, p2 := nmXY(lat, lon, r.lat1, r.lon1), nmXY(lat, lon, r.lat2, r.lon2)
		d := math.Hypot((p1.x+p2.x)/2, (p1.y+p2.y)/2)
		if d < best && d <= mapHomeNM {
			home, best = r.airport, d
		}
		if d <= float64(rangeNM)+2 {
			inView = append(inView, rw{r, p1, p2, d})
		}
	}
	sort.SliceStable(inView, func(i, j int) bool { return inView[i].dist < inView[j].dist })
	for _, r := range inView {
		add(toPx(r.p1), toPx(r.p2), true)
	}
	if rangeNM <= mapCentreMaxRange && home != "" {
		tick := 1.0
		if rangeNM > 10 {
			tick = 5
		}
		var ticks [][2]pt
		for _, r := range inView {
			if r.r.airport != home {
				continue
			}
			for _, end := range [][2]pt{{r.p1, r.p2}, {r.p2, r.p1}} { // approaches to each end: out beyond it
				thr, far := end[0], end[1]
				l := math.Hypot(thr.x-far.x, thr.y-far.y)
				if l < 1e-6 {
					continue
				}
				u := pt{(thr.x - far.x) / l, (thr.y - far.y) / l}
				out := pt{thr.x + u.x*mapCentreNM, thr.y + u.y*mapCentreNM}
				add(toPx(thr), toPx(out), false)
				for k := tick; k <= mapCentreNM; k += tick {
					c := toPx(pt{thr.x + u.x*k, thr.y + u.y*k})
					n := pt{u.y * mapTickPx, u.x * mapTickPx} // perpendicular in pixels (y down)
					ticks = append(ticks, [2]pt{{c.x - n.x, c.y - n.y}, {c.x + n.x, c.y + n.y}})
				}
			}
		}
		for _, t := range ticks { // after every centreline: if the cap bites, ticks go first
			add(t[0], t[1], false)
		}
	}
	if len(m.segs) == 0 {
		m.flags |= mNone
	}
	return m
}

func mapHandler() http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		if !versionOK(w, r) {
			return
		}
		q, err := parseQuery(r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		m := buildMap(q.lat, q.lon, q.rangeNM)
		writeBinary(w, m.encode())
		log.Printf("%s %s -> %d segments, flags %#x, %v", r.RemoteAddr, r.URL.RequestURI(), len(m.segs), m.flags,
			time.Since(start).Round(time.Millisecond))
	}
}

// packMaps writes the emulator's map pack (src/feed_fake.c): "FM", u16 count, then per cart range (5, 10, 25,
// 50, 100 nm) u16 length and the /v1/map body, padded to an even length.
func packMaps(out string, lat, lon float64) error {
	ranges := []int{5, 10, 25, 50, 100}
	buf := []byte{'F', 'M', 0, byte(len(ranges))}
	for _, r := range ranges {
		m := buildMap(lat, lon, r)
		body := m.encode()
		buf = binary.BigEndian.AppendUint16(buf, uint16(len(body)))
		buf = append(buf, body...)
		if len(buf)%2 == 1 {
			buf = append(buf, 0)
		}
		log.Printf("%d nm: %d segments", r, len(m.segs))
	}
	if len(buf) > 1<<16 {
		return fmt.Errorf("%s: %d bytes", out, len(buf))
	}
	log.Printf("%s: %d bytes", out, len(buf))
	return os.WriteFile(out, buf, 0o644)
}
