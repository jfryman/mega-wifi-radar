package main

// Cockpit mode's terrain patch (lode/ui/cockpit.md): GET /v1/terrain?icao=abfcda&v=3 returns struct
// ar_terrain, then tn*tn height levels and tn*tn palette indices, rows from the south edge, each row west
// to east (src/arframe.h). The patch is centred on the aircraft; its cell size follows the aircraft's
// height above the ground, so the camera sits about cellsAbove cells up at any altitude. Positions in it
// come from /v1/track (track.go), keyed by the patch's key.

import (
	"context"
	"errors"
	"fmt"
	"log"
	"math"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"time"
)

const (
	terrainPath   = "/v1/terrain"
	trackPath     = "/v1/track"
	tn            = 128 // AR_TN: cells per side
	terrainHdr    = 104 // AR_TERRAIN_HDR
	terrainSize   = terrainHdr + 2*tn*tn
	levels        = 64 // AR_LEVELS: heights 0..63
	keyLen        = 56 // AR_KEY_LEN
	tFlat         = 0x01
	seaBelowM     = -5 // elevation below this is sea (bathymetry), not a lake artefact
	cellsAbove    = 5  // camera height in cells: the cell size is the aircraft's height above ground / this
	minCellM      = 30 // on the ground / very low
	maxCellM      = 3000
	metresPerDegN = 111320.0
)

// Palette indices (the cart's PAL0). 0 is unused, 1-3 are the sky.
const (
	cWater = 4 + iota // 4, 5
	_
	cLow // 6, 7, 8: dark, mid, light
	_
	_
	cMid // 9, 10, 11
	_
	_
	cHigh // 12, 13
	_
	cUrban // 14 (reserved for land cover)
	cSnow  // 15
)

// 0x0BGR, 3 bits per channel.
var terrainPal = [16]uint16{
	0x000, 0xA42, 0xC86, 0xECA, // -, sky deep, mid, pale
	0x620, 0xA60, // water
	0x042, 0x064, 0x286, // low ground
	0x022, 0x242, 0x264, // forest / mid ground
	0x246, 0x468, // high ground
	0x666, 0xEEE, // urban, snow
}

// geom is a patch's placement: everything /v1/track needs to put an aircraft in it.
type geom struct {
	lat, lon float64 // centre
	cellM    int
	levelCm  int
	baseM    int
}

func (g geom) key() string {
	return fmt.Sprintf("%.5f,%.5f,%d,%d,%d", g.lat, g.lon, g.cellM, g.levelCm, g.baseM)
}

func parseKey(s string) (geom, error) {
	p := strings.Split(s, ",")
	if len(p) != 5 {
		return geom{}, errors.New("patch key: want lat,lon,cell,level,base")
	}
	var g geom
	var err [5]error
	g.lat, err[0] = strconv.ParseFloat(p[0], 64)
	g.lon, err[1] = strconv.ParseFloat(p[1], 64)
	g.cellM, err[2] = strconv.Atoi(p[2])
	g.levelCm, err[3] = strconv.Atoi(p[3])
	g.baseM, err[4] = strconv.Atoi(p[4])
	for _, e := range err {
		if e != nil {
			return geom{}, fmt.Errorf("patch key: %w", e)
		}
	}
	if math.Abs(g.lat) > 85 || math.Abs(g.lon) > 180 || g.cellM <= 0 || g.levelCm <= 0 {
		return geom{}, errors.New("patch key: out of range")
	}
	return g, nil
}

// cellXY is lat, lon in cells east / north of the patch's south-west corner.
func (g geom) cellXY(lat, lon float64) (float64, float64) {
	e := (lon - g.lon) * metresPerDegN * math.Cos(g.lat*math.Pi/180)
	n := (lat - g.lat) * metresPerDegN
	return e/float64(g.cellM) + tn/2, n/float64(g.cellM) + tn/2
}

// latLon is the centre of cell (col, row).
func (g geom) latLon(col, row int) (float64, float64) {
	e := (float64(col) + 0.5 - tn/2) * float64(g.cellM)
	n := (float64(row) + 0.5 - tn/2) * float64(g.cellM)
	return g.lat + n/metresPerDegN, g.lon + e/(metresPerDegN*math.Cos(g.lat*math.Pi/180))
}

// level is metres above sea level as a height level (fractional, unclamped).
func (g geom) level(m float64) float64 {
	return (m - float64(g.baseM)) * 100 / float64(g.levelCm)
}

type patch struct {
	g      geom
	flags  uint8
	hmax   uint8
	height [tn * tn]uint8
	colour [tn * tn]uint8
}

// cellFor picks the cell size for an aircraft aglM metres above the ground.
func cellFor(aglM float64) int {
	return int(math.Round(math.Min(math.Max(aglM/cellsAbove, minCellM), maxCellM)))
}

// buildPatch samples elevation for a patch centred on lat, lon. cellM 0 picks the size from altM (metres
// above sea level; NaN = on the ground). Without elevation data the patch is flat (tFlat).
func buildPatch(ctx context.Context, el elevator, lat, lon, altM float64, cellM int) patch {
	var p patch
	if cellM == 0 {
		ground, err := el.elevation(ctx, 10, lat, lon)
		switch {
		case math.IsNaN(altM):
			cellM = minCellM
		case err != nil:
			cellM = cellFor(altM)
		default:
			cellM = cellFor(altM - math.Max(ground, 0))
		}
	}
	p.g = geom{lat: lat, lon: lon, cellM: cellM}
	z := demZoom(lat, float64(cellM))
	elev := make([]float64, tn*tn)
	var err error
	for r := 0; r < tn && err == nil; r++ {
		for c := 0; c < tn; c++ {
			la, lo := p.g.latLon(c, r)
			var e float64
			if e, err = el.elevation(ctx, z, la, lo); err != nil {
				break
			}
			elev[r*tn+c] = e
		}
	}
	if err != nil {
		log.Printf("terrain %.4f,%.4f: %v: serving flat ground", lat, lon, err)
		p.flags |= tFlat
		clear(elev)
	}

	// Sea: Terrarium carries bathymetry, so open water is well below 0 m; its surface is at 0 m. A few
	// metres below 0 is a void or artefact in a lake (seen near Nashville), and is ground.
	// The base is the 1st percentile of the ground, so one bad pixel can't sink the whole patch; lower
	// ground is clamped to it.
	sea := make([]bool, tn*tn)
	var land []float64
	anySea := false
	for i, e := range elev {
		if e < seaBelowM && p.flags&tFlat == 0 {
			sea[i], elev[i], anySea = true, 0, true
			continue
		}
		land = append(land, e)
	}
	lo, hi := 0.0, 0.0
	if len(land) > 0 {
		sort.Float64s(land)
		lo, hi = land[len(land)/100], land[len(land)-1]
	}
	if anySea {
		lo = math.Min(lo, 0)
	}
	for i := range elev {
		elev[i] = math.Max(elev[i], lo)
	}
	p.g.baseM = int(math.Floor(lo))
	levelM := math.Max(float64(cellM)/16, (hi-float64(p.g.baseM))/(levels-1)) // true scale, unless that won't fit
	p.g.levelCm = int(math.Ceil(levelM * 100))
	for i, e := range elev {
		h := uint8(math.Min(math.Max(math.Round(p.g.level(e)), 0), levels-1))
		p.height[i] = h
		p.hmax = max(p.hmax, h)
	}
	p.colourise(elev, sea, lo, hi)
	return p
}

// colourise picks each cell's palette index: sea, then bands of the patch's own relief (lo..hi), lit from
// the north-west so slopes read in the voxel view.
func (p *patch) colourise(elev []float64, sea []bool, lo, hi float64) {
	span := math.Max(hi-lo, 1)
	for r := 0; r < tn; r++ {
		for c := 0; c < tn; c++ {
			i := r*tn + c
			e := elev[i]
			if sea[i] {
				p.colour[i] = uint8(cWater + (r+c)%2)
				continue
			}
			nw := elev[min(r+1, tn-1)*tn+max(c-1, 0)] // north is row+1, west is col-1
			slope := (e - nw) / float64(p.g.cellM)    // facing the light: positive
			shade := 1
			if slope > 0.03 {
				shade = 2
			} else if slope < -0.03 {
				shade = 0
			}
			t := (e - lo) / span
			switch {
			case e > 2800:
				p.colour[i] = cSnow
			case t < 0.4:
				p.colour[i] = uint8(cLow + shade)
			case t < 0.8:
				p.colour[i] = uint8(cMid + shade)
			default:
				p.colour[i] = uint8(cHigh + min(shade, 1))
			}
		}
	}
}

// encode writes struct ar_terrain and the two maps (AR_TERRAIN_SIZE bytes).
func (p *patch) encode() []byte {
	b := make([]byte, terrainSize)
	b[0], b[1], b[2], b[3] = 'A', 'T', frameVer, p.flags
	put16 := func(off int, v uint16) { b[off], b[off+1] = byte(v>>8), byte(v) }
	put16(4, tn)
	put16(6, uint16(p.g.cellM))
	put16(8, uint16(p.g.levelCm))
	put16(10, uint16(int16(p.g.baseM)))
	b[12] = p.hmax
	for i, c := range terrainPal {
		put16(14+2*i, c)
	}
	copy(b[46:46+keyLen-1], p.g.key()) // NUL-terminated: the last byte stays 0
	copy(b[terrainHdr:], p.height[:])
	copy(b[terrainHdr+tn*tn:], p.colour[:])
	return b
}

// altMetres is the aircraft's altitude above sea level (GPS, else barometric), NaN on the ground or unknown.
func altMetres(a *upstreamAC) (float64, bool) {
	if s, ok := a.AltBaro.(string); ok && s == "ground" {
		return math.NaN(), true
	}
	if a.AltGeom != nil {
		return *a.AltGeom * 0.3048, false
	}
	if f, ok := a.AltBaro.(float64); ok {
		return f * 0.3048, false
	}
	return math.NaN(), false
}

func terrainHandler(src *source, el elevator) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		if !versionOK(w, r) {
			return
		}
		hex := strings.ToLower(r.URL.Query().Get("icao"))
		if _, err := strconv.ParseUint(hex, 16, 24); err != nil || len(hex) != 6 {
			http.Error(w, "icao must be 6 hex digits", http.StatusBadRequest)
			return
		}
		a, err := src.aircraft(r.Context(), hex, trackMaxAge)
		if err != nil {
			log.Printf("%s %s: %v", r.RemoteAddr, r.URL.RequestURI(), err)
			http.Error(w, "upstream unavailable", http.StatusBadGateway)
			return
		}
		if a == nil || a.Lat == nil || a.Lon == nil {
			http.Error(w, "aircraft not in the feed", http.StatusNotFound)
			return
		}
		alt, _ := altMetres(a)
		p := buildPatch(r.Context(), el, *a.Lat, *a.Lon, alt, 0)
		body := p.encode()
		writeBinary(w, body)
		log.Printf("%s %s -> %d m cells, %d cm levels, base %d m, hmax %d, flags %#x, %d B, %v", r.RemoteAddr,
			r.URL.RequestURI(), p.g.cellM, p.g.levelCm, p.g.baseM, p.hmax, p.flags, len(body),
			time.Since(start).Round(time.Millisecond))
	}
}
