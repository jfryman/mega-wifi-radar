// Command scopegen generates the radar scope's plane-B data (lode/ui/sweep.md):
// base tiles (rings + ticks), the 26x26 tilemap, and per-angle-step sweep pixel lists.
//
//	cd tools && go run ./scopegen -o ../res/scope
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"math"
	"os"
	"path/filepath"
	"sort"
)

const (
	cells   = 26         // 26x26 tiles = 208x208 px block
	size    = cells * 8  // 208
	center  = size / 2.0 // disc centre inside the block
	radius  = 104.0      // disc radius in px
	rings   = 5          // evenly spaced range rings
	steps   = 256        // sweep angle steps per revolution
	idxRing = 1          // palette index of rings and ticks (never swept)
)

func main() {
	out := flag.String("o", "res/scope", "output directory")
	flag.Parse()
	if err := os.MkdirAll(*out, 0o755); err != nil {
		log.Fatal(err)
	}

	// Classify every pixel of the block.
	inDisc := make([]bool, size*size)
	ring := make([]bool, size*size)
	angle := make([]int, size*size)
	for y := 0; y < size; y++ {
		for x := 0; x < size; x++ {
			dx, dy := float64(x)+0.5-center, float64(y)+0.5-center
			d := math.Hypot(dx, dy)
			if d > radius+0.5 {
				continue
			}
			i := y*size + x
			inDisc[i] = true
			// clockwise from north, 256 steps
			deg := math.Mod(math.Atan2(dx, -dy)*180/math.Pi+360, 360)
			angle[i] = int(deg*steps/360) % steps
			for k := 1; k <= rings; k++ {
				if math.Abs(d-radius*float64(k)/rings) < 0.55 {
					ring[i] = true
				}
			}
			// rim ticks: every 10 deg (short), every 30 deg (long)
			if d > radius-12 {
				nearest := math.Round(deg/10) * 10
				off := math.Abs(deg-nearest) * math.Pi / 180 * d // arc distance in px
				long := int(nearest)%30 == 0
				if off < 0.6 && (d > radius-5 || (long && d > radius-11)) {
					ring[i] = true
				}
			}
		}
	}

	// Disc tiles: any tile with a disc pixel gets a unique index (1-based; 0 = blank).
	tileOf := make([]int, cells*cells)
	n := 0
	for ty := 0; ty < cells; ty++ {
		for tx := 0; tx < cells; tx++ {
			has := false
			for p := 0; p < 64 && !has; p++ {
				has = inDisc[(ty*8+p/8)*size+tx*8+p%8]
			}
			if has {
				n++
				tileOf[ty*cells+tx] = n
			}
		}
	}

	tiles := make([]byte, n*32)
	lists := make([][]uint16, steps)
	px := 0
	for y := 0; y < size; y++ {
		for x := 0; x < size; x++ {
			i := y*size + x
			if !inDisc[i] {
				continue
			}
			t := tileOf[(y/8)*cells+x/8] - 1
			p := (y%8)*8 + x%8
			if ring[i] {
				b := &tiles[t*32+p/2]
				if p%2 == 0 {
					*b |= idxRing << 4
				} else {
					*b |= idxRing
				}
				continue // rings are never swept
			}
			lists[angle[i]] = append(lists[angle[i]], uint16(t<<6|p))
			px++
		}
	}

	// Per step: nseg, then per tile segment: tile, n_even, n_odd, even byte
	// offsets, odd byte offsets (offset = tile*32 + pixel/2 into the RAM copy).
	// The ROM ORs/ANDs nibbles straight through these offsets (src/scope.c).
	var idx []uint16
	var all []uint16
	for _, l := range lists {
		sort.Slice(l, func(i, j int) bool { return l[i] < l[j] })
		idx = append(idx, uint16(len(all)))
		segAt := len(all)
		all = append(all, 0)
		for i := 0; i < len(l); {
			t := l[i] >> 6
			var even, odd []uint16
			for ; i < len(l) && l[i]>>6 == t; i++ {
				off := l[i] >> 1 // = t*32 + p/2
				if l[i]&1 == 0 {
					even = append(even, off)
				} else {
					odd = append(odd, off)
				}
			}
			all = append(all, t, uint16(len(even)), uint16(len(odd)))
			all = append(all, even...)
			all = append(all, odd...)
			all[segAt]++
		}
	}
	idx = append(idx, uint16(len(all)))

	tmap := make([]uint16, cells*cells)
	for i, t := range tileOf {
		tmap[i] = uint16(t)
	}

	write(*out, "scope_tiles.bin", tiles)
	write(*out, "scope_map.bin", be16(tmap))
	write(*out, "sweep_idx.bin", be16(idx))
	write(*out, "sweep_px.bin", be16(all))
	minS, maxS := 1<<30, 0
	for _, l := range lists {
		minS, maxS = min(minS, len(l)), max(maxS, len(l))
	}
	fmt.Printf("scopegen: %d disc tiles (%d B), %d sweepable px (%d B lists), %d-%d px/step\n",
		n, len(tiles), px, len(all)*2, minS, maxS)
}

func be16(v []uint16) []byte {
	b := make([]byte, len(v)*2)
	for i, x := range v {
		binary.BigEndian.PutUint16(b[i*2:], x)
	}
	return b
}

func write(dir, name string, data []byte) {
	if err := os.WriteFile(filepath.Join(dir, name), data, 0o644); err != nil {
		log.Fatal(err)
	}
}
