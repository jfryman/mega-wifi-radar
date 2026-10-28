package main

// Compact frame v1. Spec: lode/architecture/frame-format.md; console side: src/arframe.h.

import (
	"encoding/binary"
	"math"
	"sort"
	"strconv"
	"strings"
)

const (
	frameVer     = 1
	hdrSize      = 16
	recSize      = 24
	maxRecs      = 96
	maxRangeNM   = 250
	staleSec     = 15
	dropSec      = 60
	posPerNM     = 32 // 1/32 nm
	sourceADSBfi = 1
)

// Header flags.
const (
	hTruncated   = 0x01
	hUpstreamErr = 0x02
)

// Record flags.
const (
	fGround = 0x01
	fEmerg  = 0x02
	fStale  = 0x04
	fAltUnk = 0x08
	fGSUnk  = 0x10
	fTrkUnk = 0x20
	fVRUnk  = 0x40
)

// upstreamAC is the subset of the ADSBexchange-v2-style aircraft object we use.
type upstreamAC struct {
	Hex       string   `json:"hex"`
	Flight    string   `json:"flight"`
	Reg       string   `json:"r"`
	Lat       *float64 `json:"lat"`
	Lon       *float64 `json:"lon"`
	AltBaro   any      `json:"alt_baro"` // feet (number) or "ground"
	GS        *float64 `json:"gs"`
	Track     *float64 `json:"track"`
	BaroRate  *float64 `json:"baro_rate"`
	GeomRate  *float64 `json:"geom_rate"`
	Squawk    string   `json:"squawk"`
	Emergency string   `json:"emergency"`
	Category  string   `json:"category"`
	SeenPos   *float64 `json:"seen_pos"`
}

type upstreamResp struct {
	Now float64      `json:"now"` // ms since epoch
	AC  []upstreamAC `json:"ac"`
}

type record struct {
	icao   uint32
	flags  uint8
	x, y   int16
	alt    int16
	squawk uint16
	gs     uint8
	track  uint8
	vrate  int8
	cat    uint8
	cs     [8]byte
	distNM float64 // sort key, not encoded
}

type frame struct {
	seq     uint16
	time    uint32
	rangeNM uint16
	source  uint8
	hflags  uint8
	recs    []record
}

// buildFrame filters, projects, sorts nearest-first and caps the upstream aircraft.
func buildFrame(up *upstreamResp, lat, lon float64, rangeNM, max int, seq uint16) frame {
	recs := make([]record, 0, len(up.AC))
	for i := range up.AC {
		if r, ok := convert(&up.AC[i], lat, lon, float64(rangeNM)); ok {
			recs = append(recs, r)
		}
	}
	sort.SliceStable(recs, func(i, j int) bool { return recs[i].distNM < recs[j].distNM })
	f := frame{seq: seq, time: uint32(up.Now / 1000), rangeNM: uint16(rangeNM), source: sourceADSBfi}
	if len(recs) > max {
		recs = recs[:max]
		f.hflags |= hTruncated
	}
	f.recs = recs
	return f
}

func convert(a *upstreamAC, homeLat, homeLon, rangeNM float64) (record, bool) {
	var r record
	if a.Lat == nil || a.Lon == nil {
		return r, false
	}
	if a.SeenPos != nil && *a.SeenPos > dropSec {
		return r, false
	}
	d, brg := distBearingNM(homeLat, homeLon, *a.Lat, *a.Lon)
	if d > rangeNM {
		return r, false
	}
	icao, err := strconv.ParseUint(strings.TrimPrefix(a.Hex, "~"), 16, 24)
	if err != nil {
		return r, false
	}
	r.icao = uint32(icao)
	r.distNM = d
	r.x = clampS16(math.Round(d * math.Sin(brg) * posPerNM))
	r.y = clampS16(math.Round(d * math.Cos(brg) * posPerNM))

	switch v := a.AltBaro.(type) {
	case float64:
		r.alt = clampS16(math.Round(v / 25))
	case string:
		if v == "ground" {
			r.flags |= fGround
		} else {
			r.flags |= fAltUnk
		}
	default:
		r.flags |= fAltUnk
	}
	if a.GS != nil {
		r.gs = uint8(math.Min(math.Round(*a.GS/2), 255))
	} else {
		r.flags |= fGSUnk
	}
	if a.Track != nil {
		r.track = uint8(int(math.Round(*a.Track*256/360)) & 0xFF)
	} else {
		r.flags |= fTrkUnk
	}
	if rate := firstNonNil(a.BaroRate, a.GeomRate); rate != nil {
		r.vrate = int8(math.Max(-127, math.Min(127, math.Round(*rate/64))))
	} else {
		r.flags |= fVRUnk
	}
	r.squawk = 0xFFFF
	if len(a.Squawk) == 4 {
		if v, err := strconv.ParseUint(a.Squawk, 16, 16); err == nil {
			r.squawk = uint16(v)
		}
	}
	if (a.Emergency != "" && a.Emergency != "none") ||
		r.squawk == 0x7500 || r.squawk == 0x7600 || r.squawk == 0x7700 {
		r.flags |= fEmerg
	}
	if a.SeenPos != nil && *a.SeenPos > staleSec {
		r.flags |= fStale
	}
	r.cat = 0xFF
	if c := a.Category; len(c) == 2 && c[0] >= 'A' && c[0] <= 'P' && c[1] >= '0' && c[1] <= '9' {
		r.cat = (c[0]-'A')<<4 | (c[1] - '0')
	}
	r.cs = callsign(a)
	return r, true
}

func callsign(a *upstreamAC) [8]byte {
	s := strings.TrimSpace(a.Flight)
	if s == "" {
		s = strings.TrimSpace(a.Reg)
	}
	if s == "" {
		s = strings.TrimPrefix(a.Hex, "~")
	}
	s = strings.ToUpper(s)
	var cs [8]byte
	for i := range cs {
		cs[i] = ' '
		if i < len(s) && s[i] >= 0x20 && s[i] < 0x7F {
			cs[i] = s[i]
		}
	}
	return cs
}

// encode writes the frame big-endian, exactly as src/arframe.h lays it out.
func (f *frame) encode() []byte {
	b := make([]byte, hdrSize+recSize*len(f.recs))
	b[0], b[1], b[2], b[3] = 'A', 'R', frameVer, uint8(len(f.recs))
	be := binary.BigEndian
	be.PutUint16(b[4:], recSize)
	be.PutUint16(b[6:], f.seq)
	be.PutUint32(b[8:], f.time)
	be.PutUint16(b[12:], f.rangeNM)
	b[14], b[15] = f.source, f.hflags
	for i, r := range f.recs {
		p := b[hdrSize+i*recSize:]
		p[0], p[1], p[2], p[3] = byte(r.icao>>16), byte(r.icao>>8), byte(r.icao), r.flags
		be.PutUint16(p[4:], uint16(r.x))
		be.PutUint16(p[6:], uint16(r.y))
		be.PutUint16(p[8:], uint16(r.alt))
		be.PutUint16(p[10:], r.squawk)
		p[12], p[13], p[14], p[15] = r.gs, r.track, uint8(r.vrate), r.cat
		copy(p[16:24], r.cs[:])
	}
	return b
}

const earthRadiusNM = 3440.065

// distBearingNM returns the great-circle distance (nm) and initial bearing (radians,
// clockwise from true north): an azimuthal equidistant projection about home.
func distBearingNM(lat1, lon1, lat2, lon2 float64) (float64, float64) {
	φ1, φ2 := lat1*math.Pi/180, lat2*math.Pi/180
	dφ, dλ := φ2-φ1, (lon2-lon1)*math.Pi/180
	h := math.Sin(dφ/2)*math.Sin(dφ/2) + math.Cos(φ1)*math.Cos(φ2)*math.Sin(dλ/2)*math.Sin(dλ/2)
	d := 2 * earthRadiusNM * math.Asin(math.Min(1, math.Sqrt(h)))
	brg := math.Atan2(math.Sin(dλ)*math.Cos(φ2), math.Cos(φ1)*math.Sin(φ2)-math.Sin(φ1)*math.Cos(φ2)*math.Cos(dλ))
	return d, brg
}

func clampS16(v float64) int16 {
	return int16(math.Max(math.MinInt16, math.Min(math.MaxInt16, v)))
}

func firstNonNil(vs ...*float64) *float64 {
	for _, v := range vs {
		if v != nil {
			return v
		}
	}
	return nil
}
