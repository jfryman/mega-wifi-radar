package main

// Weather: NEXRAD precipitation sampled on the scope's cell grid (lode/ui/weather.md). The source is the
// Iowa Environmental Mesonet's n0q composite (CONUS, a scan every 5 min): its metadata gives the scan time
// (the weather id), and its WMS draws any box with the composite's own palette, so each pixel maps back to
// its value (wxpal.go). The cart sends the id it holds as w= on /v1/frame; the frame says when it differs.

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"log"
	"math"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"
)

const (
	wxPath  = "/v1/wx"
	wxN     = 26  // AR_WX_N: cells per side
	wxRow   = 7   // AR_WX_ROW: bytes per row
	wxSize  = 190 // AR_WX_SIZE
	hWx     = 0x08
	wNone   = 0x01
	wOld    = 0x02
	wxScope = 104 // the scope's radius in pixels: a cell is 8 px

	wxCheckEvery = time.Minute      // the scan time is looked up at most this often
	wxOldAfter   = 15 * time.Minute // older scans are flagged wOld
	wxKeep       = 32               // sampled grids cached
	wxNativeDeg  = 0.005            // the composite's pixel size
)

// Levels by dBZ (n0q value v is v/2 - 32.5): light 20-35, moderate 35-50, heavy 50+.
var wxLevelMin = [4]int{0, (20 + 32.5) * 2, (35 + 32.5) * 2, (50 + 32.5) * 2}

// The composite's extent: west, south, east, north.
var n0qExtent = [4]float64{-126, 23, -65, 50}

var n0qIndex = func() map[uint32]uint8 {
	m := make(map[uint32]uint8, len(n0qPalette))
	for i, c := range n0qPalette {
		m[c] = uint8(i)
	}
	return m
}()

// wxLevel turns one WMS pixel into a level: transparent (no echo) or below 20 dBZ is 0.
func wxLevel(c color.Color) uint8 {
	n := color.NRGBAModel.Convert(c).(color.NRGBA)
	if n.A < 128 {
		return 0
	}
	v, ok := n0qIndex[uint32(n.R)<<16|uint32(n.G)<<8|uint32(n.B)]
	if !ok {
		return 0
	}
	for l := 3; l > 0; l-- {
		if int(v) >= wxLevelMin[l] {
			return uint8(l)
		}
	}
	return 0
}

type bbox struct{ west, south, east, north float64 }

// wxBox is the lat/lon box the relay asks for: the scope's square around home at rangeNM, plus a margin.
func wxBox(lat, lon float64, rangeNM int) bbox {
	dLat := float64(rangeNM) / 60 * 1.05
	dLon := dLat / math.Max(0.1, math.Cos(lat*math.Pi/180))
	return bbox{lon - dLon, lat - dLat, lon + dLon, lat + dLat}
}

// size is the image size for the box: about the composite's own resolution, 64-400 px a side.
func (b bbox) size() (int, int) {
	px := func(deg float64) int { return min(max(int(math.Round(deg/wxNativeDeg)), 64), 400) }
	return px(b.east - b.west), px(b.north - b.south)
}

func (b bbox) covered() bool {
	return b.east > n0qExtent[0] && b.west < n0qExtent[2] && b.north > n0qExtent[1] && b.south < n0qExtent[3]
}

// unproject is the inverse of the frame's azimuthal equidistant projection: the point east, north nm of home.
func unproject(lat, lon, east, north float64) (float64, float64) {
	d := math.Hypot(east, north) / earthRadiusNM
	th := math.Atan2(east, north)
	p1, l1 := lat*math.Pi/180, lon*math.Pi/180
	p2 := math.Asin(math.Sin(p1)*math.Cos(d) + math.Cos(p1)*math.Sin(d)*math.Cos(th))
	l2 := l1 + math.Atan2(math.Sin(th)*math.Sin(d)*math.Cos(p1), math.Cos(d)-math.Sin(p1)*math.Sin(p2))
	return p2 * 180 / math.Pi, l2 * 180 / math.Pi
}

// sampleWx levels each scope cell from img (which covers b): 4 x 4 points per 8 px cell, projected like the
// frame; a cell gets the highest level that at least 4 of its 16 points reach. Points off the disc count as 0.
func sampleWx(img image.Image, b bbox, lat, lon float64, rangeNM int) (cells [wxN][wxN]uint8) {
	r := img.Bounds()
	w, h := float64(r.Dx()), float64(r.Dy())
	nmPerPx := float64(rangeNM) / wxScope
	for row := 0; row < wxN; row++ {
		for col := 0; col < wxN; col++ {
			var n [4]int
			for j := 0; j < 4; j++ {
				for i := 0; i < 4; i++ {
					ex, ny := float64(8*col+2*i+1)-wxScope, wxScope-float64(8*row+2*j+1)
					if ex*ex+ny*ny > wxScope*wxScope {
						continue
					}
					plat, plon := unproject(lat, lon, ex*nmPerPx, ny*nmPerPx)
					x := int(math.Floor((plon - b.west) / (b.east - b.west) * w))
					y := int(math.Floor((b.north - plat) / (b.north - b.south) * h))
					if x < 0 || y < 0 || x >= r.Dx() || y >= r.Dy() {
						continue
					}
					n[wxLevel(img.At(r.Min.X+x, r.Min.Y+y))]++
				}
			}
			for l, sum := 3, 0; l > 0; l-- {
				if sum += n[l]; sum >= 4 {
					cells[row][col] = uint8(l)
					break
				}
			}
		}
	}
	return cells
}

type wxGrid struct {
	flags   uint8
	id      uint16
	rangeNM uint16
	cells   [wxN][wxN]uint8
}

func (g *wxGrid) encode() []byte {
	b := make([]byte, wxSize)
	copy(b, "AW")
	b[2] = frameVer
	b[3] = g.flags
	binary.BigEndian.PutUint16(b[4:], g.id)
	binary.BigEndian.PutUint16(b[6:], g.rangeNM)
	for row := 0; row < wxN; row++ {
		for col := 0; col < wxN; col++ {
			b[8+row*wxRow+col/4] |= g.cells[row][col] << (6 - 2*(col%4))
		}
	}
	return b
}

// wxID is a scan's id: its unix minute, low 16 bits, never 0.
func wxID(t time.Time) uint16 {
	if id := uint16(t.Unix() / 60); id != 0 {
		return id
	}
	return 1
}

type wxKey struct {
	lat, lon int // 1e-4 degrees
	rangeNM  int
}

type wxCached struct {
	grid  wxGrid
	valid time.Time
	used  time.Time
}

// wxSource is IEM's n0q composite. A nil *wxSource (or an empty base URL) is "no weather".
type wxSource struct {
	base   string
	client *http.Client

	mu        sync.Mutex
	valid     time.Time // the newest scan's time
	checkedAt time.Time
	checking  bool
	cache     map[wxKey]wxCached
}

func newWx(base string) *wxSource {
	if base == "" {
		return nil
	}
	return &wxSource{base: base, client: &http.Client{Timeout: 20 * time.Second}, cache: map[wxKey]wxCached{}}
}

func (s *wxSource) get(ctx context.Context, url string) (*http.Response, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", userAgent)
	resp, err := s.client.Do(req)
	if err != nil {
		return nil, err
	}
	if resp.StatusCode != http.StatusOK {
		resp.Body.Close()
		return nil, fmt.Errorf("weather %s: %s", url, resp.Status)
	}
	return resp, nil
}

// latest looks up the newest scan's time (the composite's metadata).
func (s *wxSource) latest(ctx context.Context) (time.Time, error) {
	resp, err := s.get(ctx, s.base+"/data/gis/images/4326/USCOMP/n0q_0.json")
	if err != nil {
		return time.Time{}, err
	}
	defer resp.Body.Close()
	var m struct {
		Meta struct {
			Valid string `json:"valid"`
		} `json:"meta"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&m); err != nil {
		return time.Time{}, fmt.Errorf("weather metadata: %w", err)
	}
	return time.Parse(time.RFC3339, m.Meta.Valid)
}

// refresh updates the scan time if it's due; wait: in this call, else in the background.
func (s *wxSource) refresh(ctx context.Context, wait bool) {
	s.mu.Lock()
	if s.checking || time.Since(s.checkedAt) < wxCheckEvery {
		s.mu.Unlock()
		return
	}
	s.checking = true
	s.mu.Unlock()
	run := func(ctx context.Context) {
		t, err := s.latest(ctx)
		s.mu.Lock()
		defer s.mu.Unlock()
		s.checking = false
		s.checkedAt = time.Now()
		if err != nil {
			log.Printf("weather: %v", err)
			return
		}
		if !t.Equal(s.valid) {
			log.Printf("weather: scan %s (id %d)", t.Format(time.RFC3339), wxID(t))
		}
		s.valid = t
	}
	if wait {
		run(ctx)
	} else {
		go func() {
			ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
			defer cancel()
			run(ctx)
		}()
	}
}

// id is the newest scan's id, 0 if none is known yet.
func (s *wxSource) id() uint16 {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.valid.IsZero() {
		return 0
	}
	return wxID(s.valid)
}

// waiting: the request's w= isn't the newest scan (a missing w= never waits). Checks the scan time in the
// background now and then, so a frame never waits for it.
func (s *wxSource) waiting(r *http.Request) bool {
	if s == nil {
		return false
	}
	s.refresh(r.Context(), false)
	w, err := strconv.ParseUint(r.URL.Query().Get("w"), 10, 16)
	id := s.id()
	return err == nil && id != 0 && uint16(w) != id
}

// image fetches the WMS picture of b.
func (s *wxSource) image(ctx context.Context, b bbox) (image.Image, error) {
	w, h := b.size()
	url := fmt.Sprintf("%s/cgi-bin/wms/nexrad/n0q.cgi?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&LAYERS=nexrad-n0q"+
		"&SRS=EPSG:4326&BBOX=%.4f,%.4f,%.4f,%.4f&WIDTH=%d&HEIGHT=%d&FORMAT=image/png&TRANSPARENT=TRUE&STYLES=",
		s.base, b.west, b.south, b.east, b.north, w, h)
	resp, err := s.get(ctx, url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	img, err := png.Decode(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("weather image: %w", err)
	}
	return img, nil
}

// grid is the weather for a home point and range: from the cache while the scan is the newest, else fetched
// and sampled. Upstream trouble serves the last grid for the same place (wOld once it's old).
func (s *wxSource) grid(ctx context.Context, lat, lon float64, rangeNM int) (wxGrid, error) {
	if s == nil {
		return wxGrid{flags: wNone, rangeNM: uint16(rangeNM)}, nil
	}
	s.refresh(ctx, true)
	b := wxBox(lat, lon, rangeNM)
	k := wxKey{int(math.Round(lat * 1e4)), int(math.Round(lon * 1e4)), rangeNM}
	s.mu.Lock()
	valid := s.valid
	c, have := s.cache[k]
	s.mu.Unlock()
	if valid.IsZero() && !have {
		return wxGrid{}, errors.New("weather: no scan time")
	}
	if !b.covered() {
		return wxGrid{flags: wNone, id: wxID(valid), rangeNM: uint16(rangeNM)}, nil
	}
	if !have || c.valid.Before(valid) {
		img, err := s.image(ctx, b)
		if err != nil {
			if !have {
				return wxGrid{}, err
			}
			log.Printf("weather: %v (serving the scan of %s)", err, c.valid.Format(time.RFC3339))
		} else {
			c = wxCached{grid: wxGrid{id: wxID(valid), rangeNM: uint16(rangeNM),
				cells: sampleWx(img, b, lat, lon, rangeNM)}, valid: valid}
		}
	}
	c.used = time.Now()
	s.mu.Lock()
	s.cache[k] = c
	if len(s.cache) > wxKeep {
		var oldest wxKey
		var at time.Time
		for key, e := range s.cache {
			if at.IsZero() || e.used.Before(at) {
				oldest, at = key, e.used
			}
		}
		delete(s.cache, oldest)
	}
	s.mu.Unlock()
	g := c.grid
	if time.Since(c.valid) > wxOldAfter {
		g.flags |= wOld
	}
	return g, nil
}

func wxHandler(s *wxSource) http.HandlerFunc {
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
		g, err := s.grid(r.Context(), q.lat, q.lon, q.rangeNM)
		if err != nil {
			log.Printf("%s %s: %v", r.RemoteAddr, r.URL.RequestURI(), err)
			http.Error(w, "weather unavailable", http.StatusBadGateway)
			return
		}
		n := 0
		for _, row := range g.cells {
			for _, l := range row {
				if l > 0 {
					n++
				}
			}
		}
		writeBinary(w, g.encode())
		log.Printf("%s %s -> id %d, %d cells, flags %#x, %v", r.RemoteAddr, r.URL.RequestURI(), g.id, n, g.flags,
			time.Since(start).Round(time.Millisecond))
	}
}

// recordWx saves the WMS picture the relay would sample for lat, lon at rangeNM (testdata, the emulator's storm).
func recordWx(out, base string, lat, lon float64, rangeNM int) error {
	s := newWx(base)
	ctx := context.Background()
	t, err := s.latest(ctx)
	if err != nil {
		return err
	}
	b := wxBox(lat, lon, rangeNM)
	w, h := b.size()
	url := fmt.Sprintf("%s/cgi-bin/wms/nexrad/n0q.cgi?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&LAYERS=nexrad-n0q"+
		"&SRS=EPSG:4326&BBOX=%.4f,%.4f,%.4f,%.4f&WIDTH=%d&HEIGHT=%d&FORMAT=image/png&TRANSPARENT=TRUE&STYLES=",
		s.base, b.west, b.south, b.east, b.north, w, h)
	resp, err := s.get(ctx, url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	f, err := os.Create(out)
	if err != nil {
		return err
	}
	defer f.Close()
	n, err := f.ReadFrom(resp.Body)
	log.Printf("%s: scan %s, %dx%d px, %d bytes", out, t.Format(time.RFC3339), w, h, n)
	return err
}

// packWx writes the emulator's weather pack (src/feed_fake.c): "FW", u16 count, then one struct ar_wx per
// cart range (5, 10, 25, 50, 100 nm), sampled from a picture recorded with -record-wx at lat, lon, recRange.
func packWx(out, in string, lat, lon float64, recRange int, id uint16) error {
	f, err := os.Open(in)
	if err != nil {
		return err
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		return fmt.Errorf("%s: %w", in, err)
	}
	b := wxBox(lat, lon, recRange)
	ranges := []int{5, 10, 25, 50, 100}
	buf := []byte{'F', 'W', 0, byte(len(ranges))}
	for _, r := range ranges {
		g := wxGrid{id: id, rangeNM: uint16(r), cells: sampleWx(img, b, lat, lon, r)}
		buf = append(buf, g.encode()...)
	}
	log.Printf("%s: %d ranges, %d bytes", out, len(ranges), len(buf))
	return os.WriteFile(out, buf, 0o644)
}
