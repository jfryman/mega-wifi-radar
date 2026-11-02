package main

import (
	"context"
	"encoding/binary"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

// The recorded storm: IEM's WMS picture for KMCO at 100 nm (relay -record-wx), scan 2026-11-02 16:40Z.
const wxLat, wxLon, wxRec = 28.4294, -81.3090, 100

func loadWxPNG(t *testing.T) image.Image {
	t.Helper()
	f, err := os.Open("testdata/wx/kmco-100nm.png")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		t.Fatal(err)
	}
	return img
}

func rgb(c uint32) color.Color { return color.NRGBA{uint8(c >> 16), uint8(c >> 8), uint8(c), 255} }

func TestWxLevel(t *testing.T) {
	for _, c := range []struct {
		v    int // n0q value: dBZ = v/2 - 32.5
		want uint8
	}{{0, 0}, {104, 0}, {105, 1}, {134, 1}, {135, 2}, {164, 2}, {165, 3}, {200, 3}} {
		if got := wxLevel(rgb(n0qPalette[c.v])); got != c.want {
			t.Errorf("value %d (%.1f dBZ): level %d, want %d", c.v, float64(c.v)/2-32.5, got, c.want)
		}
	}
	if wxLevel(color.NRGBA{0xff, 0, 0, 0}) != 0 || wxLevel(color.NRGBA{1, 2, 3, 255}) != 0 {
		t.Error("transparent or unknown colours must be level 0")
	}
}

func TestWxUnproject(t *testing.T) {
	// The inverse of the frame's projection: back through distBearingNM gives the same offset.
	for _, p := range [][2]float64{{0, 0}, {30, 40}, {-80, 10}, {5, -99}} {
		lat, lon := unproject(wxLat, wxLon, p[0], p[1])
		d, brg := distBearingNM(wxLat, wxLon, lat, lon)
		east, north := d*math.Sin(brg), d*math.Cos(brg)
		if diff := (east-p[0])*(east-p[0]) + (north-p[1])*(north-p[1]); diff > 1e-6 {
			t.Errorf("%v nm -> %.4f,%.4f -> %.3f,%.3f nm", p, lat, lon, east, north)
		}
	}
}

func TestWxSample(t *testing.T) {
	img := loadWxPNG(t)
	b := wxBox(wxLat, wxLon, wxRec)
	if w, h := b.size(); img.Bounds().Dx() != w || img.Bounds().Dy() != h {
		t.Fatalf("recording is %v, the relay asks for %dx%d", img.Bounds(), w, h)
	}
	for _, r := range []int{5, 25, 100} {
		c := sampleWx(img, b, wxLat, wxLon, r)
		var n [4]int
		for _, row := range c {
			for _, l := range row {
				n[l]++
			}
		}
		if c[0][0] != 0 || c[0][wxN-1] != 0 || c[wxN-1][0] != 0 || c[wxN-1][wxN-1] != 0 {
			t.Errorf("range %d: corner cells (off the disc) have weather", r)
		}
		if r >= 25 && (n[1] == 0 || n[2] == 0) {
			t.Errorf("range %d: levels %v, the recorded band has light and moderate rain", r, n)
		}
		if r == 100 && n[3] == 0 {
			t.Errorf("range 100: no heavy cells, the recording has some")
		}
	}
}

func TestWxEncode(t *testing.T) {
	g := wxGrid{flags: wOld, id: 0xBEEF, rangeNM: 25}
	g.cells[0][0], g.cells[0][3], g.cells[0][4], g.cells[2][25] = 3, 1, 2, 3
	b := g.encode()
	if len(b) != wxSize || string(b[:2]) != "AW" || b[2] != frameVer || b[3] != wOld ||
		binary.BigEndian.Uint16(b[4:]) != 0xBEEF || binary.BigEndian.Uint16(b[6:]) != 25 {
		t.Fatalf("header % x", b[:8])
	}
	if b[8] != 0xC1 || b[9] != 0x80 || b[8+2*wxRow+6] != 0x30 {
		t.Errorf("cells % x / % x", b[8:8+wxRow], b[8+2*wxRow:8+3*wxRow])
	}
}

// fakeIEM serves the metadata and the recorded picture (for any box), counting picture requests.
func fakeIEM(t *testing.T, valid *atomic.Value, pics *atomic.Int32) *httptest.Server {
	data, err := os.ReadFile("testdata/wx/kmco-100nm.png")
	if err != nil {
		t.Fatal(err)
	}
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch {
		case strings.HasSuffix(r.URL.Path, "/n0q_0.json"):
			fmt.Fprintf(w, `{"meta": {"product": "N0Q", "valid": %q}}`, valid.Load().(string))
		case strings.HasSuffix(r.URL.Path, "/n0q.cgi"):
			pics.Add(1)
			w.Write(data)
		default:
			http.NotFound(w, r)
		}
	}))
}

func TestWxHandler(t *testing.T) {
	var valid atomic.Value
	var pics atomic.Int32
	valid.Store("2026-11-02T16:40:00Z")
	srv := fakeIEM(t, &valid, &pics)
	defer srv.Close()
	wx := newWx(srv.URL)
	ask := func(lat, lon float64) []byte {
		rec := httptest.NewRecorder()
		wxHandler(wx).ServeHTTP(rec, httptest.NewRequest("GET",
			fmt.Sprintf("%s?lat=%v&lon=%v&range=100&v=%d", wxPath, lat, lon, frameVer), nil))
		if rec.Code != 200 || rec.Body.Len() != wxSize {
			t.Fatalf("status %d, %d bytes", rec.Code, rec.Body.Len())
		}
		return rec.Body.Bytes()
	}
	id := wxID(time.Date(2026, 11, 2, 16, 40, 0, 0, time.UTC))
	b := ask(wxLat, wxLon)
	if binary.BigEndian.Uint16(b[4:]) != id || b[3]&wNone != 0 {
		t.Errorf("id %d flags %#x, want id %d", binary.BigEndian.Uint16(b[4:]), b[3], id)
	}
	ask(wxLat, wxLon)
	if pics.Load() != 1 {
		t.Errorf("%d picture requests for the same scan, want 1 (cached)", pics.Load())
	}
	if b := ask(51.47, -0.45); b[3]&wNone == 0 || pics.Load() != 1 { // Heathrow: outside the composite
		t.Errorf("outside coverage: flags %#x, %d picture requests", b[3], pics.Load())
	}

	// The frame flags weather the cart doesn't hold.
	src := newSource("http://upstream.invalid", time.Second)
	src.cache[query{lat: 36.2, lon: -86.7, rangeNM: 5, max: 5}] = cached{at: time.Now(), frame: frame{}}
	for _, c := range []struct {
		w    string
		want bool
	}{{"&w=0", true}, {fmt.Sprintf("&w=%d", id-1), true}, {fmt.Sprintf("&w=%d", id), false}, {"", false}} {
		rec := httptest.NewRecorder()
		frameHandler(src, nil, wx, nil).ServeHTTP(rec, httptest.NewRequest("GET",
			fmt.Sprintf("%s?lat=36.2&lon=-86.7&range=5&max=5%s&v=%d", framePath, c.w, frameVer), nil))
		if b := rec.Body.Bytes(); rec.Code != 200 || (b[15]&hWx != 0) != c.want {
			t.Errorf("frame with %q: status %d, hflags %#x, want weather %v", c.w, rec.Code, b[15], c.want)
		}
	}
}

func TestWxOff(t *testing.T) {
	var wx *wxSource // -wx ""
	g, err := wx.grid(context.Background(), wxLat, wxLon, 25)
	if err != nil || g.flags&wNone == 0 || g.id != 0 {
		t.Errorf("no source: %+v, %v", g, err)
	}
	if wx.waiting(httptest.NewRequest("GET", framePath+"?w=0", nil)) {
		t.Error("no source: the frame flags weather")
	}
}
