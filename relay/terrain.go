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
	stripPath     = "/v1/strip"
	stripCells    = 1024 // AR_STRIP_CELLS: w * h at most
	terrainLeadS  = 3.0  // centre a patch where the aircraft will be once the cart has it
	tn            = 128  // AR_TN: cells per side
	terrainHdr    = 104  // AR_TERRAIN_HDR
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

// geom is a patch's placement and scale: everything /v1/track and /v1/strip need. Its cell grid goes on past
// the patch's own 128 cells, so the cart can stream more of it (strips) as the aircraft moves, at the same
// scale and colouring.
type geom struct {
	lat, lon float64 // centre of the first patch
	cellM    int
	levelCm  int
	baseM    int
	hiM      int // top of the colour bands
}

func (g geom) key() string {
	return fmt.Sprintf("%.5f,%.5f,%d,%d,%d,%d", g.lat, g.lon, g.cellM, g.levelCm, g.baseM, g.hiM)
}

func parseKey(s string) (geom, error) {
	p := strings.Split(s, ",")
	if len(p) != 6 {
		return geom{}, errors.New("patch key: want lat,lon,cell,level,base,hi")
	}
	var g geom
	var err [6]error
	g.lat, err[0] = strconv.ParseFloat(p[0], 64)
	g.lon, err[1] = strconv.ParseFloat(p[1], 64)
	g.cellM, err[2] = strconv.Atoi(p[2])
	g.levelCm, err[3] = strconv.Atoi(p[3])
	g.baseM, err[4] = strconv.Atoi(p[4])
	g.hiM, err[5] = strconv.Atoi(p[5])
	for _, e := range err {
		if e != nil {
			return geom{}, fmt.Errorf("patch key: %w", e)
		}
	}
	if math.Abs(g.lat) > 85 || math.Abs(g.lon) > 180 || g.cellM <= 0 || g.levelCm <= 0 || g.hiM < g.baseM {
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

// latLon is the centre of cell (col, row); any col, row, not only the patch's own.
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

// cells is elevation for a w x h rectangle of a patch's grid from (x0, y0), plus each cell's north-west
// neighbour (for shading): (w+1) x (h+1) samples, column 0 is x0-1 and the last row is y0+h.
type cells struct {
	x0, y0, w, h int
	elev         []float64
}

func (c *cells) at(col, row int) float64 { return c.elev[(row-c.y0)*(c.w+1)+col-c.x0+1] }

func sampleCells(ctx context.Context, el elevator, g geom, x0, y0, w, h int) (cells, error) {
	c := cells{x0, y0, w, h, make([]float64, (w+1)*(h+1))}
	z := demZoom(g.lat, float64(g.cellM))
	for r := 0; r <= h; r++ {
		for k := 0; k <= w; k++ {
			la, lo := g.latLon(x0-1+k, y0+r)
			e, err := el.elevation(ctx, z, la, lo)
			if err != nil {
				return c, err
			}
			c.elev[r*(w+1)+k] = e
		}
	}
	return c, nil
}

// code is one cell's height level and palette index. Sea: Terrarium carries bathymetry, so open water is
// well below 0 m (a few metres below is a lake artefact, seen near Nashville, and is ground); its surface is
// 0 m. Ground below the base is clamped to it. Colours are bands of baseM..hiM, lit from the north-west so
// slopes read in the voxel view.
func (g geom) code(e, nw float64, col, row int, flat bool) (uint8, uint8) {
	lo, hi := float64(g.baseM), float64(g.hiM)
	sea := e < seaBelowM && !flat
	if sea {
		e = 0
	}
	if nw < seaBelowM && !flat {
		nw = 0
	}
	e, nw = math.Max(e, lo), math.Max(nw, lo)
	h := uint8(math.Min(math.Max(math.Round(g.level(e)), 0), levels-1))
	if sea {
		return h, uint8(cWater + (row+col)&1)
	}
	slope := (e - nw) / float64(g.cellM) // facing the light: positive
	shade := 1
	if slope > 0.03 {
		shade = 2
	} else if slope < -0.03 {
		shade = 0
	}
	t := (e - lo) / math.Max(hi-lo, 1)
	switch {
	case e > 2800:
		return h, cSnow
	case t < 0.4:
		return h, uint8(cLow + shade)
	case t < 0.8:
		return h, uint8(cMid + shade)
	}
	return h, uint8(cHigh + min(shade, 1))
}

// fill writes the rectangle's heights and colours, row by row from the south, each row west to east.
func (g geom) fill(c *cells, flat bool, height, colour []uint8) {
	for r := 0; r < c.h; r++ {
		for k := 0; k < c.w; k++ {
			col, row := c.x0+k, c.y0+r
			height[r*c.w+k], colour[r*c.w+k] = g.code(c.at(col, row), c.at(col-1, row+1), col, row, flat)
		}
	}
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
	// The centre as the key spells it (5 decimals), so strips sample exactly where the patch did.
	p.g = geom{lat: math.Round(lat*1e5) / 1e5, lon: math.Round(lon*1e5) / 1e5, cellM: cellM}
	c, err := sampleCells(ctx, el, p.g, 0, 0, tn, tn)
	flat := err != nil
	if flat {
		log.Printf("terrain %.4f,%.4f: %v: serving flat ground", lat, lon, err)
		p.flags |= tFlat
		clear(c.elev)
	}

	// The base is the 1st percentile of the patch's ground, so one bad pixel can't sink it; sea pulls it to 0.
	var land []float64
	anySea := false
	for r := 0; r < tn; r++ {
		for k := 0; k < tn; k++ {
			if e := c.at(k, r); e < seaBelowM && !flat {
				anySea = true
			} else {
				land = append(land, e)
			}
		}
	}
	lo, hi := 0.0, 0.0
	if len(land) > 0 {
		sort.Float64s(land)
		lo, hi = land[len(land)/100], land[len(land)-1]
	}
	if anySea {
		lo = math.Min(lo, 0)
	}
	p.g.baseM, p.g.hiM = int(math.Floor(lo)), int(math.Ceil(math.Max(hi, lo)))
	levelM := math.Max(float64(cellM)/16, (hi-float64(p.g.baseM))/(levels-1)) // true scale, unless that won't fit
	p.g.levelCm = int(math.Ceil(levelM * 100))
	p.g.fill(&c, flat, p.height[:], p.colour[:])
	for _, h := range p.height {
		p.hmax = max(p.hmax, h)
	}
	return p
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
		lat, lon := *a.Lat, *a.Lon
		if a.GS != nil && a.Track != nil { // where it will be when the download is done
			d, t := *a.GS*0.514444*terrainLeadS, *a.Track*math.Pi/180
			lat += d * math.Cos(t) / metresPerDegN
			lon += d * math.Sin(t) / (metresPerDegN * math.Cos(lat*math.Pi/180))
		}
		p := buildPatch(r.Context(), el, lat, lon, alt, 0)
		body := p.encode()
		writeBinary(w, body)
		log.Printf("%s %s -> %d m cells, %d cm levels, base %d m, hmax %d, flags %#x, %d B, %v", r.RemoteAddr,
			r.URL.RequestURI(), p.g.cellM, p.g.levelCm, p.g.baseM, p.hmax, p.flags, len(body),
			time.Since(start).Round(time.Millisecond))
	}
}

// stripHandler serves GET /v1/strip?p=<key>&x=&y=&w=&h=&v=3: w*h height levels, then w*h palette indices, for a
// rectangle of a patch's grid (any x, y: the grid goes on past the patch), row by row from the south, each
// row west to east. Cockpit mode streams strips into its wrapping 128 x 128 buffer as the aircraft moves.
// Without elevation data it fails (502): the cart keeps what it has and asks again.
func stripHandler(el elevator) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		if !versionOK(w, r) {
			return
		}
		q := r.URL.Query()
		g, err := parseKey(q.Get("p"))
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		var v [4]int
		for i, k := range []string{"x", "y", "w", "h"} {
			if v[i], err = strconv.Atoi(q.Get(k)); err != nil {
				http.Error(w, k+" must be an integer", http.StatusBadRequest)
				return
			}
		}
		x, y, cw, ch := v[0], v[1], v[2], v[3]
		if cw < 1 || ch < 1 || cw > tn || ch > tn || cw*ch > stripCells || abs(x) > 30000 || abs(y) > 30000 {
			http.Error(w, "strip out of range", http.StatusBadRequest)
			return
		}
		c, err := sampleCells(r.Context(), el, g, x, y, cw, ch)
		if err != nil {
			log.Printf("%s %s: %v", r.RemoteAddr, r.URL.RequestURI(), err)
			http.Error(w, "elevation unavailable", http.StatusBadGateway)
			return
		}
		body := make([]byte, 2*cw*ch)
		g.fill(&c, false, body[:cw*ch], body[cw*ch:])
		writeBinary(w, body)
		log.Printf("%s %s -> %d B, %v", r.RemoteAddr, r.URL.RequestURI(), len(body), time.Since(start).Round(time.Millisecond))
	}
}

func abs(n int) int {
	if n < 0 {
		return -n
	}
	return n
}
