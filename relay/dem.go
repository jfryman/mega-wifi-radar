package main

// Elevation for cockpit mode's terrain patches (terrain.go): Terrarium-encoded PNG tiles, by default the
// public AWS Terrain Tiles (https://registry.opendata.aws/terrain-tiles/; no key). A pixel's elevation in
// metres is R*256 + G + B/256 - 32768. Tiles are web-mercator, 256 px; decoded tiles stay in memory.

import (
	"context"
	"fmt"
	"image"
	"image/png"
	"math"
	"net/http"
	"sync"
	"time"
)

const (
	demTileSize = 256
	demMaxZoom  = 14
	demMaxTiles = 48 // decoded tiles kept (256 KB each)
)

type elevator interface {
	// elevation in metres above sea level at lat, lon, from tiles at zoom z.
	elevation(ctx context.Context, z int, lat, lon float64) (float64, error)
}

type tileKey struct{ z, x, y int }

type demTile struct {
	elev []float32
	used time.Time
}

type dem struct {
	baseURL string // .../terrarium, tiles at /{z}/{x}/{y}.png
	client  *http.Client

	mu    sync.Mutex
	tiles map[tileKey]*demTile
}

func newDEM(baseURL string) *dem {
	return &dem{baseURL: baseURL, client: &http.Client{Timeout: 10 * time.Second}, tiles: map[tileKey]*demTile{}}
}

// demZoom is the coarsest zoom whose pixels are no bigger than cellM at this latitude.
func demZoom(lat float64, cellM float64) int {
	mpp0 := 156543.03 * math.Cos(lat*math.Pi/180) // metres per pixel at zoom 0
	z := int(math.Ceil(math.Log2(mpp0 / cellM)))
	return min(max(z, 0), demMaxZoom)
}

// mercatorPx is the global pixel position of lat, lon at zoom z.
func mercatorPx(z int, lat, lon float64) (float64, float64) {
	n := float64(int(1)<<z) * demTileSize
	lat = math.Max(-85.05, math.Min(85.05, lat))
	r := lat * math.Pi / 180
	x := (lon + 180) / 360 * n
	y := (1 - math.Log(math.Tan(r)+1/math.Cos(r))/math.Pi) / 2 * n
	return x, y
}

func (d *dem) elevation(ctx context.Context, z int, lat, lon float64) (float64, error) {
	px, py := mercatorPx(z, lat, lon)
	px, py = px-0.5, py-0.5 // pixel centres
	x0, y0 := math.Floor(px), math.Floor(py)
	fx, fy := px-x0, py-y0
	var e [4]float64
	for i := range e {
		v, err := d.pixel(ctx, z, int(x0)+i%2, int(y0)+i/2)
		if err != nil {
			return 0, err
		}
		e[i] = v
	}
	return (e[0]*(1-fx)+e[1]*fx)*(1-fy) + (e[2]*(1-fx)+e[3]*fx)*fy, nil
}

func (d *dem) pixel(ctx context.Context, z, x, y int) (float64, error) {
	n := int(1) << z * demTileSize
	x = ((x % n) + n) % n
	y = min(max(y, 0), n-1)
	t, err := d.tile(ctx, tileKey{z, x / demTileSize, y / demTileSize})
	if err != nil {
		return 0, err
	}
	return float64(t.elev[(y%demTileSize)*demTileSize+x%demTileSize]), nil
}

func (d *dem) tile(ctx context.Context, k tileKey) (*demTile, error) {
	d.mu.Lock()
	if t, ok := d.tiles[k]; ok {
		t.used = time.Now()
		d.mu.Unlock()
		return t, nil
	}
	d.mu.Unlock()

	url := fmt.Sprintf("%s/%d/%d/%d.png", d.baseURL, k.z, k.x, k.y)
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", userAgent)
	resp, err := d.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("dem %s: %s", url, resp.Status)
	}
	img, err := png.Decode(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("dem %s: %w", url, err)
	}
	t := &demTile{elev: terrarium(img), used: time.Now()}

	d.mu.Lock()
	defer d.mu.Unlock()
	if len(d.tiles) >= demMaxTiles { // drop the least recently used
		var oldK tileKey
		var old *demTile
		for k, v := range d.tiles {
			if old == nil || v.used.Before(old.used) {
				oldK, old = k, v
			}
		}
		delete(d.tiles, oldK)
	}
	d.tiles[k] = t
	return t, nil
}

// terrarium decodes a Terrarium tile: metres = R*256 + G + B/256 - 32768.
func terrarium(img image.Image) []float32 {
	b := img.Bounds()
	out := make([]float32, demTileSize*demTileSize)
	for y := 0; y < demTileSize && y < b.Dy(); y++ {
		for x := 0; x < demTileSize && x < b.Dx(); x++ {
			r, g, bl, _ := img.At(b.Min.X+x, b.Min.Y+y).RGBA()
			out[y*demTileSize+x] = float32(float64(r>>8)*256 + float64(g>>8) + float64(bl>>8)/256 - 32768)
		}
	}
	return out
}
