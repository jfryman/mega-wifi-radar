// Command relay serves the mega_wifi_radar compact frame over plain HTTP.
// See lode/relay/relay.md.
package main

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"math"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"
)

const userAgent = "mega_wifi_radar-relay"

type query struct {
	lat, lon float64
	rangeNM  int
	max      int
}

// source fetches upstream JSON at most once per minInterval (adsb.fi allows 1 req/s)
// and keeps the last good result per query for failures.
type source struct {
	baseURL     string
	client      *http.Client
	minInterval time.Duration

	mu       sync.Mutex
	lastCall time.Time
	seq      uint16
	cache    map[query]cached
}

type cached struct {
	at    time.Time
	frame frame
}

func newSource(baseURL string, minInterval time.Duration) *source {
	return &source{
		baseURL:     baseURL,
		client:      &http.Client{Timeout: 10 * time.Second},
		minInterval: minInterval,
		cache:       map[query]cached{},
	}
}

func (s *source) frame(ctx context.Context, q query) (frame, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if c, ok := s.cache[q]; ok && time.Since(c.at) < s.minInterval {
		return c.frame, nil
	}
	if wait := s.minInterval - time.Since(s.lastCall); wait > 0 {
		select {
		case <-time.After(wait):
		case <-ctx.Done():
			return frame{}, ctx.Err()
		}
	}
	s.lastCall = time.Now()
	up, err := s.fetch(ctx, q)
	if err != nil {
		if c, ok := s.cache[q]; ok {
			f := c.frame
			f.hflags |= hUpstreamErr
			return f, nil
		}
		return frame{}, err
	}
	s.seq++
	f := buildFrame(up, q.lat, q.lon, q.rangeNM, q.max, s.seq)
	s.cache[q] = cached{at: time.Now(), frame: f}
	return f, nil
}

func (s *source) fetch(ctx context.Context, q query) (*upstreamResp, error) {
	url := fmt.Sprintf("%s/api/v3/lat/%.4f/lon/%.4f/dist/%d", s.baseURL, q.lat, q.lon, q.rangeNM)
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", userAgent)
	resp, err := s.client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("upstream %s: %s", url, resp.Status)
	}
	var up upstreamResp
	if err := json.NewDecoder(resp.Body).Decode(&up); err != nil {
		return nil, fmt.Errorf("upstream %s: %w", url, err)
	}
	return &up, nil
}

func parseQuery(r *http.Request) (query, error) {
	v := r.URL.Query()
	lat, err1 := strconv.ParseFloat(v.Get("lat"), 64)
	lon, err2 := strconv.ParseFloat(v.Get("lon"), 64)
	if err1 != nil || err2 != nil || math.Abs(lat) > 90 || math.Abs(lon) > 180 {
		return query{}, errors.New("lat and lon are required, in degrees")
	}
	q := query{lat: lat, lon: lon, rangeNM: 25, max: maxRecs}
	if n, err := strconv.Atoi(v.Get("range")); err == nil {
		q.rangeNM = min(max(n, 1), maxRangeNM)
	}
	if n, err := strconv.Atoi(v.Get("max")); err == nil {
		q.max = min(max(n, 1), maxRecs)
	}
	return q, nil
}

func frameHandler(src *source) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		q, err := parseQuery(r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		f, err := src.frame(r.Context(), q)
		if err != nil {
			log.Printf("%s %s: %v", r.RemoteAddr, r.URL.RequestURI(), err)
			http.Error(w, "upstream unavailable", http.StatusBadGateway)
			return
		}
		body := f.encode()
		w.Header().Set("Content-Type", "application/octet-stream")
		// Explicit length: frames can exceed net/http's 2 KiB buffer, which would
		// otherwise switch to chunked encoding; the console wants Content-Length.
		w.Header().Set("Content-Length", strconv.Itoa(len(body)))
		w.Write(body)
		log.Printf("%s %s -> %d ac, %d B, seq %d, flags %#x, %v", r.RemoteAddr, r.URL.RequestURI(),
			len(f.recs), len(body), f.seq, f.hflags, time.Since(start).Round(time.Millisecond))
	}
}

func main() {
	listen := flag.String("listen", ":8080", "HTTP listen address")
	upstream := flag.String("upstream", "https://opendata.adsb.fi", "adsb.fi-compatible base URL")
	interval := flag.Duration("min-interval", time.Second, "minimum time between upstream requests")
	convertIn := flag.String("convert", "", "convert an upstream JSON file to a frame and exit")
	pack := flag.String("pack", "", "pack the upstream JSON files given as arguments into a fake-feed file and exit")
	out := flag.String("o", "frame.bin", "output file for -convert")
	lat := flag.Float64("lat", 36.1245, "home latitude for -convert")
	lon := flag.Float64("lon", -86.6782, "home longitude for -convert")
	rng := flag.Int("range", 25, "range in nm for -convert")
	flag.Parse()

	if *pack != "" {
		if err := packFiles(*pack, flag.Args(), *lat, *lon, *rng); err != nil {
			log.Fatal(err)
		}
		return
	}
	if *convertIn != "" {
		if err := convertFile(*convertIn, *out, *lat, *lon, *rng); err != nil {
			log.Fatal(err)
		}
		return
	}

	mux := http.NewServeMux()
	mux.Handle("GET /v1/frame", frameHandler(newSource(*upstream, *interval)))
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) { fmt.Fprintln(w, "ok") })
	log.Printf("relay listening on %s (upstream %s)", *listen, *upstream)
	log.Fatal(http.ListenAndServe(*listen, mux))
}

func loadFrame(in string, lat, lon float64, rangeNM int, seq uint16) ([]byte, int, error) {
	data, err := os.ReadFile(in)
	if err != nil {
		return nil, 0, err
	}
	var up upstreamResp
	if err := json.Unmarshal(data, &up); err != nil {
		return nil, 0, fmt.Errorf("%s: %w", in, err)
	}
	f := buildFrame(&up, lat, lon, rangeNM, maxRecs, seq)
	return f.encode(), len(f.recs), nil
}

func convertFile(in, out string, lat, lon float64, rangeNM int) error {
	body, n, err := loadFrame(in, lat, lon, rangeNM, 1)
	if err != nil {
		return err
	}
	log.Printf("%s -> %s: %d aircraft, %d bytes", in, out, n, len(body))
	return os.WriteFile(out, body, 0o644)
}

// packFiles writes the fake-feed pack the ROM embeds (src/feed_fake.c):
// "FK", u16 count, count x u32 offsets from the start of the file, then the frames
// (each padded to an even length so every frame stays word-aligned).
func packFiles(out string, ins []string, lat, lon float64, rangeNM int) error {
	if len(ins) == 0 {
		return fmt.Errorf("-pack needs input JSON files")
	}
	hdr := 4 + 4*len(ins)
	buf := make([]byte, hdr)
	copy(buf, "FK")
	binary.BigEndian.PutUint16(buf[2:], uint16(len(ins)))
	for i, in := range ins {
		body, n, err := loadFrame(in, lat, lon, rangeNM, uint16(i+1))
		if err != nil {
			return err
		}
		binary.BigEndian.PutUint32(buf[4+4*i:], uint32(len(buf)))
		buf = append(buf, body...)
		if len(buf)%2 == 1 {
			buf = append(buf, 0)
		}
		log.Printf("%s: %d aircraft, %d bytes", in, n, len(body))
	}
	log.Printf("%s: %d frames, %d bytes", out, len(ins), len(buf))
	return os.WriteFile(out, buf, 0o644)
}
