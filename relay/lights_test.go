package main

import (
	"bytes"
	"context"
	"image"
	"image/color"
	"image/jpeg"
	"net/http"
	"net/http/httptest"
	"testing"
)

// eastLights: a bright city east of lon 0.2, dark to the west.
type eastLights struct{}

func (eastLights) lum(_ context.Context, _, lon float64) (uint8, error) {
	if lon > 0.2 {
		return 250, nil
	}
	return 10, nil
}

func TestLitCell(t *testing.T) {
	count := func(lum uint8) (n int) {
		for r := 0; r < 8; r++ {
			for c := 0; c < 8; c++ {
				if litCell(lum, c, r) {
					n++
				}
			}
		}
		return n
	}
	for _, c := range []struct {
		lum  uint8
		want int
	}{{255, 16}, {lightsCore, 16}, {lightsCity, 8}, {lightsSuburb, 4}, {lightsSuburb - 1, 0}} {
		if n := count(c.lum); n != c.want {
			t.Errorf("luminance %d: %d of 64 cells lit, want %d", c.lum, n, c.want)
		}
	}
}

func TestLitCellsAreUrban(t *testing.T) {
	nightLights = eastLights{}
	defer func() { nightLights = nil }()
	ctx := context.Background()
	p := buildPatch(ctx, rampDEM{}, 0.5, 0.2, 5000, 0)
	west, east := p.colour[64*tn+10], p.colour[64*tn+120] // col 120, row 64: both even, a lit cell of the pattern
	if west == cUrban || east != cUrban {
		t.Errorf("west %d east %d: want only the east urban (%d)", west, east, cUrban)
	}
	// A strip lights the same cells as the patch.
	c, err := sampleCells(ctx, rampDEM{}, p.g, 100, 64, 8, 1)
	if err != nil {
		t.Fatal(err)
	}
	h, col := make([]uint8, 8), make([]uint8, 8)
	p.g.fill(&c, false, h, col)
	for k := range col {
		if col[k] != p.colour[64*tn+100+k] {
			t.Fatalf("strip cell %d: %d, patch %d", k, col[k], p.colour[64*tn+100+k])
		}
	}
}

func TestLightTiles(t *testing.T) {
	img := image.NewRGBA(image.Rect(0, 0, 256, 256))
	for y := 0; y < 256; y++ {
		for x := 0; x < 256; x++ {
			if x >= 128 { // the tile's east half is a bright city
				img.Set(x, y, color.RGBA{250, 220, 150, 255})
			} else {
				img.Set(x, y, color.RGBA{10, 10, 30, 255})
			}
		}
	}
	var buf bytes.Buffer
	if err := jpeg.Encode(&buf, img, nil); err != nil {
		t.Fatal(err)
	}
	var paths []string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		paths = append(paths, r.URL.Path)
		w.Write(buf.Bytes())
	}))
	defer srv.Close()
	l := newLights(srv.URL)
	// KBNA sits in tile column 66, row 100 at zoom 8: west and east parts of that tile.
	west, err := l.lum(context.Background(), 36.12, -87.10)
	if err != nil {
		t.Fatal(err)
	}
	east, _ := l.lum(context.Background(), 36.12, -86.30)
	if west >= lightsSuburb || east < lightsCore {
		t.Errorf("west %d east %d: want a dark west and a bright east", west, east)
	}
	if len(paths) != 1 || paths[0] != "/8/100/66.jpeg" {
		t.Errorf("requests %v: want one, /8/100/66.jpeg (z/row/col)", paths)
	}
}

func TestSkyUrban(t *testing.T) {
	if skyPalette(skyDay)[cUrban] != terrainPal[cUrban] {
		t.Error("by day, city cells keep the grey")
	}
	if n := skyPalette(skyNight)[cUrban]; n != 0x2AE {
		t.Errorf("night city colour %#03x, want warm orange-yellow 0x2AE", n)
	}
}
