// Command splashgen turns the boot splash art (PNG pages rendered from res/splash/src/art.typ)
// into Genesis data (lode/ui/boot-splash.md): 4bpp tiles, tilemaps, sprite tiles and palettes.
//
//	cd tools && go run ./splashgen -png ../out/splash -src ../res/splash/src -o ../res/splash -h ../src/splash_data.h
//
// Pages: splash-1.png scene (320x224, PAL0), splash-2.png wordmark (288x48, PAL1),
// splash-3..6.png the 787 frames (96x48, PAL2).
package main

import (
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"image"
	"image/png"
	"log"
	"os"
	"path/filepath"
	"strings"
)

const (
	skyRows    = 150 // scene rows above the horizon: dithered over the sky colours
	shineSlots = 12  // wordmark body slots for the palette-cycled shine, spread once across it
	shineFirst = 3   // first body slot index in PAL1
	planeW     = 96
	planeH     = 48
	sprW       = 4 // sprite size in tiles (4x3 = 32x24 px), 3x2 sprites per frame
	sprH       = 3
	frames     = 4
)

type rgb struct{ r, g, b int }

func loadPalettes(path string) (map[string][]rgb, []string) {
	raw, err := os.ReadFile(path)
	if err != nil {
		log.Fatal(err)
	}
	var top map[string]json.RawMessage
	if err := json.Unmarshal(raw, &top); err != nil {
		log.Fatal(err)
	}
	var order []string
	if err := json.Unmarshal(top["order"], &order); err != nil {
		log.Fatal(err)
	}
	out := map[string][]rgb{}
	for _, name := range order {
		var p struct {
			Colors [][3]int `json:"colors"`
		}
		if err := json.Unmarshal(top[name], &p); err != nil {
			log.Fatal(err)
		}
		if len(p.Colors) != 16 {
			log.Fatalf("palette %s: %d colours, want 16", name, len(p.Colors))
		}
		for _, c := range p.Colors {
			out[name] = append(out[name], rgb{c[0], c[1], c[2]})
		}
	}
	return out, order
}

// level 0..7 -> 8-bit, as art.typ draws it.
func to8(l int) int { return (l*255 + 3) / 7 }

func dist(a rgb, l rgb) int {
	dr, dg, db := a.r-to8(l.r), a.g-to8(l.g), a.b-to8(l.b)
	return 3*dr*dr + 4*dg*dg + 2*db*db
}

// nearest index among cand (palette indices).
func nearest(c rgb, pal []rgb, cand []int) (int, int) {
	best, bd := cand[0], 1<<62
	for _, i := range cand {
		if d := dist(c, pal[i]); d < bd {
			best, bd = i, d
		}
	}
	return best, bd
}

var bayer = [4][4]int{{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}}

func readPNG(path string) image.Image {
	f, err := os.Open(path)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		log.Fatalf("%s: %v", path, err)
	}
	return img
}

func px(img image.Image, x, y int) (rgb, int) {
	r, g, b, a := img.At(img.Bounds().Min.X+x, img.Bounds().Min.Y+y).RGBA()
	if a == 0 {
		return rgb{}, 0
	}
	// un-premultiply
	return rgb{int(r * 255 / a), int(g * 255 / a), int(b * 255 / a)}, int(a >> 8)
}

func span(a, b int) []int {
	var s []int
	for i := a; i <= b; i++ {
		s = append(s, i)
	}
	return s
}

// indexed image -> 4bpp tiles in row-major tile order.
func tile(idx []uint8, w, tx, ty int) [32]byte {
	var t [32]byte
	for y := 0; y < 8; y++ {
		for x := 0; x < 8; x += 2 {
			p := (ty*8+y)*w + tx*8 + x
			t[y*4+x/2] = idx[p]<<4 | idx[p+1]
		}
	}
	return t
}

// tiles + map with dedupe; tile 0 is always blank.
func tilemap(idx []uint8, w, h int) ([][32]byte, []uint16) {
	tiles := [][32]byte{{}}
	seen := map[[32]byte]uint16{{}: 0}
	var m []uint16
	for ty := 0; ty < h/8; ty++ {
		for tx := 0; tx < w/8; tx++ {
			t := tile(idx, w, tx, ty)
			i, ok := seen[t]
			if !ok {
				i = uint16(len(tiles))
				seen[t] = i
				tiles = append(tiles, t)
			}
			m = append(m, i)
		}
	}
	return tiles, m
}

func write(path string, data any) {
	f, err := os.Create(path)
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	if err := binary.Write(f, binary.BigEndian, data); err != nil {
		log.Fatal(err)
	}
}

func main() {
	pngDir := flag.String("png", "out/splash", "rendered PNG pages")
	src := flag.String("src", "res/splash/src", "palettes.json directory")
	out := flag.String("o", "res/splash", "output directory")
	hdr := flag.String("h", "src/splash_data.h", "generated header")
	flag.Parse()
	pals, order := loadPalettes(filepath.Join(*src, "palettes.json"))
	page := func(n int) image.Image { return readPNG(filepath.Join(*pngDir, fmt.Sprintf("splash-%d.png", n))) }

	// scene: exact colours stay; gradients dither over the sky colours above the horizon
	bg := page(1)
	const W, H = 320, 224
	bgIdx := make([]uint8, W*H)
	for y := 0; y < H; y++ {
		for x := 0; x < W; x++ {
			c, _ := px(bg, x, y)
			i, d := nearest(c, pals["bg"], span(0, 15))
			if d > 60 && y < skyRows {
				t := (bayer[y&3][x&3] - 8) * 3
				c2 := rgb{c.r + t, c.g + t, c.b + t}
				i, _ = nearest(c2, pals["bg"], span(1, 7))
			}
			bgIdx[y*W+x] = uint8(i)
		}
	}
	bgTiles, bgMap := tilemap(bgIdx, W, H)

	// wordmark: body re-indexed into diagonal shine slots
	lg := page(2)
	const LW, LH = 288, 48
	lgIdx := make([]uint8, LW*LH)
	for y := 0; y < LH; y++ {
		for x := 0; x < LW; x++ {
			c, a := px(lg, x, y)
			if a < 128 {
				continue
			}
			i, _ := nearest(c, pals["logo"], []int{1, 2, shineFirst})
			if i == shineFirst {
				// one diagonal band per slot across the whole wordmark, so a single glint sweeps it
				s := (x - y/2 + LH/2) * shineSlots / (LW + LH/2)
				i += min(max(s, 0), shineSlots-1)
			}
			lgIdx[y*LW+x] = uint8(i)
		}
	}
	lgTiles, lgMap := tilemap(lgIdx, LW, LH)

	// 787 frames: 3x2 sprites of 4x3 tiles, each sprite's tiles column-major (VDP order)
	var plTiles [][32]byte
	for f := 0; f < frames; f++ {
		im := page(3 + f)
		idx := make([]uint8, planeW*planeH)
		for y := 0; y < planeH; y++ {
			for x := 0; x < planeW; x++ {
				c, a := px(im, x, y)
				if a < 128 {
					continue
				}
				i, _ := nearest(c, pals["plane"], span(1, 12))
				idx[y*planeW+x] = uint8(i)
			}
		}
		for sy := 0; sy < planeH/(sprH*8); sy++ {
			for sx := 0; sx < planeW/(sprW*8); sx++ {
				for tx := 0; tx < sprW; tx++ {
					for ty := 0; ty < sprH; ty++ {
						plTiles = append(plTiles, tile(idx, planeW, sx*sprW+tx, sy*sprH+ty))
					}
				}
			}
		}
	}

	var palWords []uint16
	for _, name := range order {
		for _, c := range pals[name] {
			palWords = append(palWords, uint16(c.b*2)<<8|uint16(c.g*2)<<4|uint16(c.r*2))
		}
	}

	if err := os.MkdirAll(*out, 0o755); err != nil {
		log.Fatal(err)
	}
	write(filepath.Join(*out, "bg_tiles.bin"), bgTiles)
	write(filepath.Join(*out, "bg_map.bin"), bgMap)
	write(filepath.Join(*out, "logo_tiles.bin"), lgTiles)
	write(filepath.Join(*out, "logo_map.bin"), lgMap)
	write(filepath.Join(*out, "plane_tiles.bin"), plTiles)
	write(filepath.Join(*out, "pal.bin"), palWords)

	var b strings.Builder
	b.WriteString("/* Generated by tools/splashgen from res/splash/src/art.typ. */\n")
	fmt.Fprintf(&b, "#define SPLASH_BG_TILES     %d\n", len(bgTiles))
	fmt.Fprintf(&b, "#define SPLASH_LOGO_TILES   %d\n", len(lgTiles))
	fmt.Fprintf(&b, "#define SPLASH_LOGO_W       %d\n", LW/8)
	fmt.Fprintf(&b, "#define SPLASH_LOGO_H       %d\n", LH/8)
	fmt.Fprintf(&b, "#define SPLASH_PLANE_FRAMES %d\n", frames)
	fmt.Fprintf(&b, "#define SPLASH_PLANE_FRAME_TILES %d  /* 6 sprites x 4x3 tiles */\n", len(plTiles)/frames)
	fmt.Fprintf(&b, "#define SPLASH_SHINE_FIRST  %d\n", shineFirst)
	fmt.Fprintf(&b, "#define SPLASH_SHINE_SLOTS  %d\n", shineSlots)
	if err := os.WriteFile(*hdr, []byte(b.String()), 0o644); err != nil {
		log.Fatal(err)
	}
	log.Printf("splashgen: bg %d tiles, logo %d tiles, plane %d tiles", len(bgTiles), len(lgTiles), len(plTiles))
}
