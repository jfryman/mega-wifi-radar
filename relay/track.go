package main

// Cockpit mode's camera feed (lode/ui/cockpit.md): GET /v1/track?icao=abfcda&p=<patch key>&v=3 returns one
// struct ar_track: the aircraft placed in the cart's current terrain patch (terrain.go), in cells and
// height levels, with its velocity, so the cart only adds and never does geodesy.

import (
	"context"
	"log"
	"math"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	trackSize   = 40
	trackMaxAge = 1500 * time.Millisecond // the cached snapshot is fresh enough for the camera
	kFound      = 0x01
	kGround     = 0x02
	kEmerg      = 0x04
	kAltUnk     = 0x08
	kNotice     = 0x10
	eyeM        = 3.0  // camera height on the ground
	maxBankDeg  = 45.0 // estimated bank is clamped to this
	gravity     = 9.81
)

type trackRec struct {
	flags      uint8
	x, y, z    int32 // 1/256 cell, 1/256 cell, 1/256 level
	vx, vy, vz int16 // per second
	heading    uint16
	bank       int8
	age        uint8
	alt        int16 // 25 ft
	gs         uint16
	vrate      int16 // ft/min
	cs         [8]byte
}

func (t *trackRec) encode() []byte {
	b := make([]byte, trackSize)
	b[0], b[1], b[2], b[3] = 'A', 'K', frameVer, t.flags
	put16 := func(off int, v uint16) { b[off], b[off+1] = byte(v>>8), byte(v) }
	put32 := func(off int, v uint32) { put16(off, uint16(v>>16)); put16(off+2, uint16(v)) }
	put32(4, uint32(t.x))
	put32(8, uint32(t.y))
	put32(12, uint32(t.z))
	put16(16, uint16(t.vx))
	put16(18, uint16(t.vy))
	put16(20, uint16(t.vz))
	put16(22, t.heading)
	b[24], b[25] = byte(t.bank), t.age
	put16(26, uint16(t.alt))
	put16(28, t.gs)
	put16(30, uint16(t.vrate))
	copy(b[32:40], t.cs[:])
	return b
}

// turns remembers each aircraft's last track, to estimate bank when upstream reports neither roll nor
// track rate.
type turns struct {
	mu   sync.Mutex
	last map[string]turnObs
}

type turnObs struct {
	track float64
	at    time.Time
}

func newTurns() *turns { return &turns{last: map[string]turnObs{}} }

// rate is the turn rate in degrees per second since the last observation of hex (0 if none usable).
func (t *turns) rate(hex string, track float64, at time.Time) float64 {
	t.mu.Lock()
	defer t.mu.Unlock()
	prev, ok := t.last[hex]
	t.last[hex] = turnObs{track, at}
	if len(t.last) > 512 {
		for k, v := range t.last {
			if at.Sub(v.at) > time.Minute {
				delete(t.last, k)
			}
		}
	}
	dt := at.Sub(prev.at).Seconds()
	if !ok || dt < 0.5 || dt > 30 {
		return 0
	}
	d := math.Mod(track-prev.track+540, 360) - 180 // -180..180
	return d / dt
}

func sat16(v float64) int16 {
	return int16(math.Max(math.Min(math.Round(v), math.MaxInt16), math.MinInt16))
}

// buildTrack places a in patch g. el gives the ground under it (on the ground or altitude unknown);
// tr estimates bank from successive tracks; at is when upstream last heard the position.
func buildTrack(ctx context.Context, a *upstreamAC, g geom, el elevator, tr *turns, hex string, at time.Time) trackRec {
	t := trackRec{flags: kFound}
	padASCII(t.cs[:], strings.TrimSpace(a.Flight))
	if a.Lat == nil || a.Lon == nil {
		return trackRec{}
	}
	x, y := g.cellXY(*a.Lat, *a.Lon)
	t.x, t.y = int32(math.Round(x*256)), int32(math.Round(y*256))

	altM, ground := altMetres(a)
	if ground {
		t.flags |= kGround
	}
	if math.IsNaN(altM) {
		if !ground {
			t.flags |= kAltUnk
		}
		e, err := el.elevation(ctx, demZoom(g.lat, float64(g.cellM)), *a.Lat, *a.Lon)
		if err != nil {
			e = float64(g.baseM)
		}
		altM = math.Max(e, 0) + eyeM
	} else {
		t.alt = clampS16(math.Round(altM / 0.3048 / 25))
		// never under the ground: altitude errors near it put the camera inside the terrain (one colour)
		if e, err := el.elevation(ctx, demZoom(g.lat, float64(g.cellM)), *a.Lat, *a.Lon); err == nil {
			altM = math.Max(altM, math.Max(e, 0)+eyeM)
		}
	}
	if ground {
		t.alt = 0
	}
	t.z = int32(math.Round(g.level(altM) * 256))

	levelM := float64(g.levelCm) / 100
	if a.GS != nil {
		t.gs = uint16(math.Round(*a.GS))
	}
	if a.GS != nil && a.Track != nil {
		mps := *a.GS * 0.514444
		r := *a.Track * math.Pi / 180
		t.vx = sat16(mps * math.Sin(r) / float64(g.cellM) * 256)
		t.vy = sat16(mps * math.Cos(r) / float64(g.cellM) * 256)
	}
	rate := a.GeomRate
	if rate == nil {
		rate = a.BaroRate
	}
	if rate != nil && !ground {
		t.vrate = clampS16(*rate)
		t.vz = sat16(*rate * 0.00508 / levelM * 256)
	}

	hdg := 0.0
	switch {
	case a.TrueHeading != nil:
		hdg = *a.TrueHeading
	case a.Track != nil:
		hdg = *a.Track
	}
	t.heading = uint16(int(math.Round(math.Mod(hdg+360, 360)/360*65536)) & 0xFFFF)

	bank := 0.0
	switch {
	case ground:
	case a.Roll != nil:
		bank = *a.Roll
	case a.GS != nil && a.Track != nil:
		w := 0.0
		if a.TrackRate != nil {
			w = *a.TrackRate
		} else {
			w = tr.rate(hex, *a.Track, at)
		}
		bank = math.Atan(*a.GS*0.514444*w*math.Pi/180/gravity) * 180 / math.Pi
	}
	t.bank = int8(math.Round(math.Max(math.Min(bank, maxBankDeg), -maxBankDeg)))

	if a.SeenPos != nil {
		t.age = uint8(math.Min(math.Round(*a.SeenPos), 255))
	}
	if a.Emergency != "" && a.Emergency != "none" {
		t.flags |= kEmerg
	}
	return t
}

func trackHandler(src *source, el elevator, tr *turns, ns *notices) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !versionOK(w, r) {
			return
		}
		q := r.URL.Query()
		hex := strings.ToLower(q.Get("icao"))
		if _, err := strconv.ParseUint(hex, 16, 24); err != nil || len(hex) != 6 {
			http.Error(w, "icao must be 6 hex digits", http.StatusBadRequest)
			return
		}
		g, err := parseKey(q.Get("p"))
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		a, err := src.aircraft(r.Context(), hex, trackMaxAge)
		if err != nil {
			log.Printf("%s %s: %v", r.RemoteAddr, r.URL.RequestURI(), err)
			http.Error(w, "upstream unavailable", http.StatusBadGateway)
			return
		}
		var t trackRec
		if a != nil {
			at := time.Now()
			if a.SeenPos != nil {
				at = at.Add(-time.Duration(*a.SeenPos * float64(time.Second)))
			}
			t = buildTrack(r.Context(), a, g, el, tr, hex, at)
		}
		if ns.waiting(r) {
			t.flags |= kNotice
		}
		writeBinary(w, t.encode())
		log.Printf("%s %s -> found %v, cell %.1f,%.1f z %.1f, bank %d", r.RemoteAddr, r.URL.RequestURI(),
			t.flags&kFound != 0, float64(t.x)/256, float64(t.y)/256, float64(t.z)/256, t.bank)
	}
}
