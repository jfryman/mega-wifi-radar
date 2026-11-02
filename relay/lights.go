package main

// Night lights for cockpit mode (lode/ui/cockpit.md): cells under city lights get palette index 14 (cUrban),
// which the sky phase colours grey by day and warm yellow at night (sky.go). The source is NASA GIBS's
// VIIRS City Lights 2012 (NASA Earth Observatory / NOAA NGDC): web-mercator JPEG tiles at zoom 8 (~500 m a
// pixel), no key. -lights "" turns it off; a failed tile only leaves those cells unlit.

import (
	"context"
	"fmt"
	"image/jpeg"
	"net/http"
	"sync"
	"time"
)

const (
	lightsZoom     = 8  // the layer's finest level
	lightsMaxTiles = 16 // decoded tiles kept (64 KB each)
	// Lights are points from the air, so lit cells are a sparse pattern whose density follows the brightness
	// (luminance 0..255): one cell in four over city centres, one in eight over the rest of the city, one in
	// sixteen over the outskirts. A solid fill read as a yellow slab, and a checkerboard was still too dense.
	lightsCore   = 200
	lightsCity   = 120
	lightsSuburb = 70
)

// nightLights is the source sampleCells asks; nil (tests, -lights "") leaves every cell unlit.
var nightLights lighter

type lighter interface {
	lum(ctx context.Context, lat, lon float64) (uint8, error) // 0 dark .. 255 brightest
}

// litCell decides from a cell's luminance and its grid position (so a strip agrees with its patch).
func litCell(lum uint8, col, row int) bool {
	switch {
	case lum >= lightsCore:
		return col&1 == 0 && row&1 == 0
	case lum >= lightsCity:
		return col&3 == 0 && row&1 == 0
	case lum >= lightsSuburb:
		return col&3 == 0 && row&3 == 0
	}
	return false
}

type lightTile struct {
	lum  []uint8
	used time.Time
}

type lightMap struct {
	baseURL string // .../GoogleMapsCompatible_Level8, tiles at /{z}/{row}/{col}.jpeg
	client  *http.Client

	mu    sync.Mutex
	tiles map[tileKey]*lightTile
}

func newLights(baseURL string) *lightMap {
	return &lightMap{baseURL: baseURL, client: &http.Client{Timeout: 10 * time.Second}, tiles: map[tileKey]*lightTile{}}
}

func (l *lightMap) lum(ctx context.Context, lat, lon float64) (uint8, error) {
	px, py := mercatorPx(lightsZoom, lat, lon)
	n := int(1) << lightsZoom * demTileSize
	x, y := ((int(px)%n)+n)%n, min(max(int(py), 0), n-1)
	t, err := l.tile(ctx, tileKey{lightsZoom, x / demTileSize, y / demTileSize})
	if err != nil {
		return 0, err
	}
	return t.lum[(y%demTileSize)*demTileSize+x%demTileSize], nil
}

func (l *lightMap) tile(ctx context.Context, k tileKey) (*lightTile, error) {
	l.mu.Lock()
	if t, ok := l.tiles[k]; ok {
		t.used = time.Now()
		l.mu.Unlock()
		return t, nil
	}
	l.mu.Unlock()

	url := fmt.Sprintf("%s/%d/%d/%d.jpeg", l.baseURL, k.z, k.y, k.x) // WMTS: matrix, row, column
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", userAgent)
	resp, err := l.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("lights %s: %s", url, resp.Status)
	}
	img, err := jpeg.Decode(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("lights %s: %w", url, err)
	}
	t := &lightTile{lum: make([]uint8, demTileSize*demTileSize), used: time.Now()}
	b := img.Bounds()
	for y := 0; y < demTileSize && y < b.Dy(); y++ {
		for x := 0; x < demTileSize && x < b.Dx(); x++ {
			r, g, bl, _ := img.At(b.Min.X+x, b.Min.Y+y).RGBA()
			t.lum[y*demTileSize+x] = uint8((299*(r>>8) + 587*(g>>8) + 114*(bl>>8)) / 1000)
		}
	}

	l.mu.Lock()
	defer l.mu.Unlock()
	if len(l.tiles) >= lightsMaxTiles { // drop the least recently used
		var oldK tileKey
		var old *lightTile
		for k, v := range l.tiles {
			if old == nil || v.used.Before(old.used) {
				oldK, old = k, v
			}
		}
		delete(l.tiles, oldK)
	}
	l.tiles[k] = t
	return t, nil
}
