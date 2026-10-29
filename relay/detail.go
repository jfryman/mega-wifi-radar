package main

// Aircraft detail for the cart's pop-up: GET /v1/detail?icao=abfcda&v=2 returns one fixed-size
// struct ar_detail (src/arframe.h; lode/ui/detail-popup.md). The live values (position, altitude,
// speed) stay in the frame; this carries what the frame has no room for.

import (
	"context"
	"fmt"
	"log"
	"math"
	"net/http"
	"strconv"
	"strings"
	"time"
)

const (
	detailSize = 96
	// The cart asks right after a frame poll, so the cached upstream snapshot normally has the
	// aircraft. Older than this, or missing, and the relay asks upstream for that one aircraft.
	detailMaxAge = 30 * time.Second
)

// Detail flags (AR_D_*).
const (
	dFound   = 0x01
	dGeomAlt = 0x02
	dSelAlt  = 0x04
	dSelHdg  = 0x08
	dQNH     = 0x10
	dYear    = 0x20
	dSeen    = 0x40
)

type detail struct {
	flags   uint8
	icao    uint32
	cat     uint8
	geomAlt int16 // 25 ft
	selAlt  int16 // 25 ft
	selHdg  uint16
	qnh     uint16 // 0.1 hPa
	year    uint16
	seen    uint16 // s
	reg     [8]byte
	typ     [4]byte
	desc    [32]byte
	owner   [32]byte
}

func buildDetail(icao uint32, a *upstreamAC) detail {
	d := detail{icao: icao, cat: catNone}
	for _, b := range [][]byte{d.reg[:], d.typ[:], d.desc[:], d.owner[:]} {
		padASCII(b, "")
	}
	if a == nil {
		return d
	}
	d.flags |= dFound
	d.cat = category(a.Category)
	if a.AltGeom != nil {
		d.geomAlt, d.flags = clampS16(math.Round(*a.AltGeom/25)), d.flags|dGeomAlt
	}
	if a.NavAlt != nil {
		d.selAlt, d.flags = clampS16(math.Round(*a.NavAlt/25)), d.flags|dSelAlt
	}
	if a.NavHdg != nil {
		d.selHdg, d.flags = uint16(int(math.Round(*a.NavHdg))%360), d.flags|dSelHdg
	}
	if a.NavQNH != nil {
		d.qnh, d.flags = uint16(math.Round(*a.NavQNH*10)), d.flags|dQNH
	}
	if y, err := strconv.Atoi(a.Year); err == nil && y > 1900 && y < 3000 {
		d.year, d.flags = uint16(y), d.flags|dYear
	}
	if a.Seen != nil {
		d.seen, d.flags = uint16(math.Min(math.Round(*a.Seen), math.MaxUint16)), d.flags|dSeen
	}
	padASCII(d.reg[:], strings.TrimSpace(a.Reg))
	padASCII(d.typ[:], strings.TrimSpace(a.Type))
	padASCII(d.desc[:], strings.TrimSpace(a.Desc))
	padASCII(d.owner[:], strings.TrimSpace(a.OwnOp))
	return d
}

// encode writes the record big-endian, exactly as struct ar_detail lays it out.
func (d *detail) encode() []byte {
	b := make([]byte, detailSize)
	b[0], b[1], b[2], b[3] = 'A', 'D', frameVer, d.flags
	b[4], b[5], b[6], b[7] = byte(d.icao>>16), byte(d.icao>>8), byte(d.icao), d.cat
	put16 := func(off int, v uint16) { b[off], b[off+1] = byte(v>>8), byte(v) }
	put16(8, uint16(d.geomAlt))
	put16(10, uint16(d.selAlt))
	put16(12, d.selHdg)
	put16(14, d.qnh)
	put16(16, d.year)
	copy(b[18:26], d.reg[:])
	copy(b[26:30], d.typ[:])
	put16(30, d.seen)
	copy(b[32:64], d.desc[:])
	copy(b[64:96], d.owner[:])
	return b
}

// aircraft finds one aircraft: in the newest cached snapshot if it's recent, else upstream.
func (s *source) aircraft(ctx context.Context, hex string) (*upstreamAC, error) {
	s.mu.Lock()
	if s.last != nil && time.Since(s.lastAt) < detailMaxAge {
		for i := range s.last.AC {
			if strings.EqualFold(strings.TrimPrefix(s.last.AC[i].Hex, "~"), hex) {
				a := s.last.AC[i]
				s.mu.Unlock()
				return &a, nil
			}
		}
	}
	s.mu.Unlock()
	up, err := s.get(ctx, fmt.Sprintf("%s/api/v2/hex/%s", s.baseURL, hex))
	if err != nil {
		return nil, err
	}
	for i := range up.AC {
		if strings.EqualFold(strings.TrimPrefix(up.AC[i].Hex, "~"), hex) {
			return &up.AC[i], nil
		}
	}
	return nil, nil
}

func detailHandler(src *source) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !versionOK(w, r) {
			return
		}
		hex := strings.ToLower(r.URL.Query().Get("icao"))
		icao, err := strconv.ParseUint(hex, 16, 24)
		if err != nil || len(hex) != 6 {
			http.Error(w, "icao must be 6 hex digits", http.StatusBadRequest)
			return
		}
		a, err := src.aircraft(r.Context(), hex)
		if err != nil {
			log.Printf("%s %s: %v", r.RemoteAddr, r.URL.RequestURI(), err)
			http.Error(w, "upstream unavailable", http.StatusBadGateway)
			return
		}
		d := buildDetail(uint32(icao), a)
		writeBinary(w, d.encode())
		log.Printf("%s %s -> found %v", r.RemoteAddr, r.URL.RequestURI(), a != nil)
	}
}
