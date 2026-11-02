// Command geoidgen writes relay/geoid.bin: the EGM96 geoid undulation (metres above the WGS84 ellipsoid) on a
// 1-degree grid, for turning ADS-B's geometric altitude (height above the ellipsoid) into height above sea level
// in cockpit mode (lode/plans/cockpit-low-altitude.md). 181 rows from 90N to 90S, 361 columns from 0E to 360E
// (the last repeats the first), one int8 per point, rounded to the metre. Bilinear on this grid is within 2.4 m
// of the 5-minute grid for 99% of the globe. Source: GeographicLib's egm96-5 (MIT; EGM96 is NGA, public domain).
//
//	cd tools && go run ./geoidgen -o ../relay/geoid.bin
package main

import (
	"archive/tar"
	"bufio"
	"bytes"
	"compress/bzip2"
	"flag"
	"fmt"
	"io"
	"log"
	"math"
	"net/http"
	"os"
	"strconv"
	"strings"
)

const url = "https://sourceforge.net/projects/geographiclib/files/geoids-distrib/egm96-5.tar.bz2/download"

// pgm reads GeographicLib's geoid PGM: comments carry Offset and Scale, then width, height, maxval, then
// big-endian u16 samples from 90N, 0E.
func pgm(b []byte) (w, h int, at func(r, c int) float64, err error) {
	rd := bufio.NewReader(bytes.NewReader(b))
	offset, scale := 0.0, 1.0
	var nums []int
	for len(nums) < 3 {
		line, e := rd.ReadString('\n')
		if e != nil {
			return 0, 0, nil, e
		}
		line = strings.TrimSpace(line)
		switch {
		case line == "P5" || line == "":
		case strings.HasPrefix(line, "# Offset "):
			offset, _ = strconv.ParseFloat(strings.TrimPrefix(line, "# Offset "), 64)
		case strings.HasPrefix(line, "# Scale "):
			scale, _ = strconv.ParseFloat(strings.TrimPrefix(line, "# Scale "), 64)
		case strings.HasPrefix(line, "#"):
		default:
			for _, f := range strings.Fields(line) {
				n, e := strconv.Atoi(f)
				if e != nil {
					return 0, 0, nil, e
				}
				nums = append(nums, n)
			}
		}
	}
	w, h = nums[0], nums[1]
	data, err := io.ReadAll(rd)
	if err != nil || len(data) < 2*w*h {
		return 0, 0, nil, fmt.Errorf("pgm: %d bytes of samples, want %d", len(data), 2*w*h)
	}
	return w, h, func(r, c int) float64 {
		i := 2 * (r*w + c%w)
		return offset + scale*float64(uint16(data[i])<<8|uint16(data[i+1]))
	}, nil
}

func fetch(local string) []byte {
	if local != "" {
		b, err := os.ReadFile(local)
		if err != nil {
			log.Fatal(err)
		}
		return b
	}
	resp, err := http.Get(url)
	if err != nil {
		log.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		log.Fatalf("%s: %s", url, resp.Status)
	}
	tr := tar.NewReader(bzip2.NewReader(resp.Body))
	for {
		hdr, err := tr.Next()
		if err != nil {
			log.Fatalf("egm96-5.pgm not in the archive: %v", err)
		}
		if strings.HasSuffix(hdr.Name, "egm96-5.pgm") {
			b, err := io.ReadAll(tr)
			if err != nil {
				log.Fatal(err)
			}
			return b
		}
	}
}

func main() {
	out := flag.String("o", "geoid.bin", "output file")
	local := flag.String("pgm", "", "use this egm96-5.pgm instead of downloading GeographicLib's archive")
	flag.Parse()

	w, h, at, err := pgm(fetch(*local))
	if err != nil {
		log.Fatal(err)
	}
	step := w / 360 // samples per degree
	if step*360 != w || (h-1)/step != 180 {
		log.Fatalf("unexpected grid %d x %d", w, h)
	}
	buf := make([]byte, 0, 181*361)
	for r := 0; r <= 180; r++ {
		for c := 0; c <= 360; c++ {
			buf = append(buf, byte(int8(math.Round(at(r*step, c*step)))))
		}
	}
	if err := os.WriteFile(*out, buf, 0o644); err != nil {
		log.Fatal(err)
	}
	log.Printf("%s: %d bytes", *out, len(buf))
}
