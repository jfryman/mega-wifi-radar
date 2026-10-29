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
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

const userAgent = "mega_wifi_radar-relay"

// version is the release this relay ships with (set by the image build: -ldflags -X main.version).
// The cart it pairs with is the same release; they check each other by frameVer (AR_VER).
var version = "dev"

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
	last     *upstreamResp // newest snapshot, for /v1/detail
	lastAt   time.Time
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
	s.last, s.lastAt = up, time.Now()
	s.seq++
	f := buildFrame(up, q.lat, q.lon, q.rangeNM, q.max, s.seq)
	s.cache[q] = cached{at: time.Now(), frame: f}
	return f, nil
}

func (s *source) fetch(ctx context.Context, q query) (*upstreamResp, error) {
	return s.do(ctx, fmt.Sprintf("%s/api/v3/lat/%.4f/lon/%.4f/dist/%d", s.baseURL, q.lat, q.lon, q.rangeNM))
}

// get is a rate-limited upstream request outside the frame cache (one aircraft for /v1/detail).
func (s *source) get(ctx context.Context, url string) (*upstreamResp, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if wait := s.minInterval - time.Since(s.lastCall); wait > 0 {
		select {
		case <-time.After(wait):
		case <-ctx.Done():
			return nil, ctx.Err()
		}
	}
	s.lastCall = time.Now()
	return s.do(ctx, url)
}

func (s *source) do(ctx context.Context, url string) (*upstreamResp, error) {
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

// versionOK answers a request from a cart at another contract version with httpMismatch.
func versionOK(w http.ResponseWriter, r *http.Request) bool {
	if v := r.URL.Query().Get("v"); v != strconv.Itoa(frameVer) {
		log.Printf("%s %s: cart speaks v%q, this relay (%s) v%d", r.RemoteAddr, r.URL.RequestURI(), v, version, frameVer)
		http.Error(w, fmt.Sprintf("relay %s speaks v%d: use the relay and cartridge from the same release",
			version, frameVer), httpMismatch)
		return false
	}
	return true
}

// writeBinary sends an explicit length: frames can exceed net/http's 2 KiB buffer, which would
// otherwise switch to chunked encoding; the console wants Content-Length.
func writeBinary(w http.ResponseWriter, body []byte) {
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Length", strconv.Itoa(len(body)))
	w.Write(body)
}

func frameHandler(src *source) http.HandlerFunc {
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
		f, err := src.frame(r.Context(), q)
		if err != nil {
			log.Printf("%s %s: %v", r.RemoteAddr, r.URL.RequestURI(), err)
			http.Error(w, "upstream unavailable", http.StatusBadGateway)
			return
		}
		body := f.encode()
		writeBinary(w, body)
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
	packDetail := flag.String("pack-detail", "", "pack the details of every aircraft in the upstream JSON files given as arguments and exit")
	out := flag.String("o", "frame.bin", "output file for -convert")
	lat := flag.Float64("lat", 36.1245, "home latitude for -convert")
	lon := flag.Float64("lon", -86.6782, "home longitude for -convert")
	rng := flag.Int("range", 25, "range in nm for -convert")
	mdns := flag.Bool("mdns", true, "advertise the relay over mDNS as _mwradar._tcp (lode/net/discovery.md)")
	flag.Parse()

	if *packDetail != "" {
		if err := packDetails(*packDetail, flag.Args()); err != nil {
			log.Fatal(err)
		}
		return
	}
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
	src := newSource(*upstream, *interval)
	mux.Handle("GET "+framePath, frameHandler(src))
	mux.Handle("GET "+detailPath, detailHandler(src))
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) { fmt.Fprintln(w, "ok") })
	if *mdns {
		_, p, err := net.SplitHostPort(*listen)
		port, _ := strconv.Atoi(p)
		host, _ := os.Hostname()
		if err == nil && port > 0 {
			go serveMDNS(newAdvert(host, uint16(port)))
		}
	}
	log.Printf("relay %s (contract v%d) listening on %s (upstream %s)", version, frameVer, *listen, *upstream)
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

// packDetails writes the fake-feed detail pack (src/feed_fake.c): "FD", u16 count, then one
// struct ar_detail per aircraft, from the last file that has it.
func packDetails(out string, ins []string) error {
	byHex := map[string]upstreamAC{}
	var order []string
	for _, in := range ins {
		data, err := os.ReadFile(in)
		if err != nil {
			return err
		}
		var up upstreamResp
		if err := json.Unmarshal(data, &up); err != nil {
			return fmt.Errorf("%s: %w", in, err)
		}
		for _, a := range up.AC {
			hex := strings.ToLower(strings.TrimPrefix(a.Hex, "~"))
			if _, ok := byHex[hex]; !ok {
				order = append(order, hex)
			}
			byHex[hex] = a
		}
	}
	buf := []byte{'F', 'D', 0, 0}
	binary.BigEndian.PutUint16(buf[2:], uint16(len(order)))
	for _, hex := range order {
		icao, err := strconv.ParseUint(hex, 16, 24)
		if err != nil {
			continue
		}
		a := byHex[hex]
		d := buildDetail(uint32(icao), &a)
		buf = append(buf, d.encode()...)
	}
	binary.BigEndian.PutUint16(buf[2:], uint16((len(buf)-4)/detailSize))
	log.Printf("%s: %d aircraft, %d bytes", out, (len(buf)-4)/detailSize, len(buf))
	return os.WriteFile(out, buf, 0o644)
}
