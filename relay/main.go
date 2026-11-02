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

func frameHandler(src *source, ns *notices, wx *wxSource) http.HandlerFunc {
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
		if ns.waiting(r) {
			f.hflags |= hNotice
		}
		if wx.waiting(r) {
			f.hflags |= hWx
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
	demURL := flag.String("dem", "https://s3.amazonaws.com/elevation-tiles-prod/terrarium",
		"Terrarium elevation tiles base URL, for cockpit mode's terrain")
	packCockpit := flag.String("pack-cockpit", "", "pack a terrain patch at -lat/-lon with -cell metre cells, and every aircraft's track in it from the upstream JSON files given as arguments, and exit")
	cell := flag.Int("cell", 750, "cell size in metres for -pack-cockpit")
	mq := mqttConfig{pass: os.Getenv("MQTT_PASSWORD")} // env only: a flag would show in ps
	flag.StringVar(&mq.host, "mqtt-host", "", "MQTT broker host for flyover notices (empty: off)")
	flag.IntVar(&mq.port, "mqtt-port", 1883, "MQTT broker port")
	flag.StringVar(&mq.user, "mqtt-user", "", "MQTT user name (password: MQTT_PASSWORD)")
	flag.StringVar(&mq.topic, "mqtt-topic", "atc/status", "MQTT topic atc-mqtt publishes flights to")
	flag.BoolVar(&mq.clear, "mqtt-clear", false, "clear a retained message on the topic after reading it")
	flag.StringVar(&mq.clientID, "mqtt-client-id", mqttClientID(), "MQTT client id")
	noticeTTL := flag.Duration("notice-ttl", 30*time.Second, "how long a flyover notice waits for a cart")
	lights := flag.String("lights", "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_CityLights_2012/default/2012-01-01/GoogleMapsCompatible_Level8",
		"night-lights tile base (WMTS, /{z}/{row}/{col}.jpeg) for cockpit mode's city lights; empty: off")
	wxURL := flag.String("wx", "https://mesonet.agron.iastate.edu", "Iowa Environmental Mesonet base URL for weather (NEXRAD n0q); empty: off")
	recordWxOut := flag.String("record-wx", "", "save the weather picture for -lat/-lon/-range to this PNG and exit")
	packWxOut := flag.String("pack-wx", "", "pack the weather picture given as argument (recorded at -lat/-lon/-range) into the emulator's weather file and exit")
	wxPackID := flag.Uint("wx-id", 1, "weather id for -pack-wx")
	sky := flag.String("sky", "real", "cockpit sky: real (the sun at the aircraft, now), day, golden, twilight or night")
	envFlags(flag.CommandLine, serverEnv)
	flag.Parse()
	var err error
	if skyOverride, err = parseSky(*sky); err != nil {
		log.Fatal(err)
	}

	el := newDEM(*demURL)
	if *lights != "" {
		nightLights = newLights(*lights)
	}
	if *packCockpit != "" {
		if err := packCockpitFile(*packCockpit, flag.Args(), el, *lat, *lon, *cell); err != nil {
			log.Fatal(err)
		}
		return
	}

	if *recordWxOut != "" {
		if err := recordWx(*recordWxOut, *wxURL, *lat, *lon, *rng); err != nil {
			log.Fatal(err)
		}
		return
	}
	if *packWxOut != "" {
		if flag.NArg() != 1 {
			log.Fatal("-pack-wx needs one recorded PNG")
		}
		if err := packWx(*packWxOut, flag.Arg(0), *lat, *lon, *rng, uint16(*wxPackID)); err != nil {
			log.Fatal(err)
		}
		return
	}
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
	ns := newNotices(src, *noticeTTL)
	wx := newWx(*wxURL)
	mux.Handle("GET "+framePath, frameHandler(src, ns, wx))
	mux.Handle("GET "+wxPath, wxHandler(wx))
	mux.Handle("GET "+noticePath, noticeHTTP(ns))
	mux.Handle("GET "+detailPath, detailHandler(src))
	mux.Handle("GET "+terrainPath, terrainHandler(src, el))
	mux.Handle("GET "+trackPath, trackHandler(src, el, newTurns(), ns))
	mux.Handle("GET "+stripPath, stripHandler(el))
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) { fmt.Fprintln(w, "ok") })
	if *mdns {
		_, p, err := net.SplitHostPort(*listen)
		port, _ := strconv.Atoi(p)
		host, _ := os.Hostname()
		if err == nil && port > 0 {
			go serveMDNS(newAdvert(host, uint16(port)))
		}
	}
	if mq.host != "" {
		log.Printf("mqtt: %s, topic %q, user %q, client %s", mq.addr(), mq.topic, mq.user, mq.clientID)
		go runMQTT(context.Background(), mq, ns.handle)
	} else {
		log.Printf("mqtt: off (no MQTT_BROKER_HOST): no flyover notices")
	}
	if wx == nil {
		log.Printf("weather: off (no -wx)")
	}
	log.Printf("relay %s (contract v%d) listening on %s (upstream %s)", version, frameVer, *listen, *upstream)
	log.Fatal(http.ListenAndServe(*listen, mux))
}

// serverEnv maps the server's flags to environment variables, for containers and the cluster. The MQTT
// names are atc-mqtt's own, so both services can share one secret / config map.
var serverEnv = map[string]string{
	"listen":         "RELAY_LISTEN",
	"upstream":       "RELAY_UPSTREAM",
	"min-interval":   "RELAY_MIN_INTERVAL",
	"mdns":           "RELAY_MDNS",
	"dem":            "RELAY_DEM",
	"mqtt-host":      "MQTT_BROKER_HOST",
	"mqtt-port":      "MQTT_BROKER_PORT",
	"mqtt-user":      "MQTT_USERNAME",
	"mqtt-topic":     "MQTT_TOPIC",
	"mqtt-clear":     "MQTT_CLEAR",
	"mqtt-client-id": "MQTT_CLIENT_ID",
	"notice-ttl":     "RELAY_NOTICE_TTL",
	"sky":            "RELAY_SKY",
	"lights":         "RELAY_LIGHTS",
	"wx":             "RELAY_WX",
}

// envFlags sets flags from non-empty environment variables before Parse, so a command-line flag
// still wins over the variable, which wins over the default.
func envFlags(fs *flag.FlagSet, names map[string]string) {
	for name, env := range names {
		if v := os.Getenv(env); v != "" {
			if err := fs.Set(name, v); err != nil {
				log.Fatalf("%s=%q: %v", env, v, err)
			}
		}
	}
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

// packCockpitFile writes the emulator's cockpit pack (src/feed_fake.c): "FC", u16 frames, u16 0, the terrain
// body for a patch centred on lat, lon (AR_TERRAIN_SIZE bytes), then per upstream file: u16 count,
// u16 deciseconds since the first file, count x { u8 icao[3], u8 0, struct ar_track } placed in that patch.
func packCockpitFile(out string, ins []string, el elevator, lat, lon float64, cellM int) error {
	if len(ins) == 0 {
		return fmt.Errorf("-pack-cockpit needs input JSON files")
	}
	ctx := context.Background()
	p := buildPatch(ctx, el, lat, lon, 0, cellM)
	if skyOverride >= 0 { // -sky: the emulator's patch at another time of day
		pal := skyPalette(skyOverride)
		p.pal = &pal
	}
	buf := []byte{'F', 'C', 0, 0, 0, 0}
	binary.BigEndian.PutUint16(buf[2:], uint16(len(ins)))
	buf = append(buf, p.encode()...)
	tr := newTurns()
	var t0 float64
	for i, in := range ins {
		data, err := os.ReadFile(in)
		if err != nil {
			return err
		}
		var up upstreamResp
		if err := json.Unmarshal(data, &up); err != nil {
			return fmt.Errorf("%s: %w", in, err)
		}
		if i == 0 {
			t0 = up.Now
		}
		at := time.UnixMilli(int64(up.Now))
		var recs []byte
		n := 0
		for j := range up.AC {
			a := &up.AC[j]
			hex := strings.ToLower(strings.TrimPrefix(a.Hex, "~"))
			if a.Lat == nil || a.Lon == nil {
				continue
			}
			t := buildTrack(ctx, a, p.g, el, tr, hex, at)
			icao, err := strconv.ParseUint(hex, 16, 24)
			if err != nil {
				continue
			}
			recs = append(recs, byte(icao>>16), byte(icao>>8), byte(icao), 0)
			recs = append(recs, t.encode()...)
			n++
		}
		hdr := make([]byte, 4)
		binary.BigEndian.PutUint16(hdr, uint16(n))
		binary.BigEndian.PutUint16(hdr[2:], uint16(math.Round((up.Now-t0)/100)))
		buf = append(buf, hdr...)
		buf = append(buf, recs...)
	}
	log.Printf("%s: %d m cells, %d cm levels, base %d m, hmax %d, flags %#x; %d frames, %d bytes", out,
		p.g.cellM, p.g.levelCm, p.g.baseM, p.hmax, p.flags, len(ins), len(buf))
	return os.WriteFile(out, buf, 0o644)
}
