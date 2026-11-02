package main

// Recordings (lode/plans/cockpit-low-altitude.md): -record saves the upstream's area JSON once per interval, one
// snapshot per line, gzipped; -replay serves such a file as the relay's upstream from a loopback server, looping,
// so cockpit mode can fly a recorded arrival on demand (the real cart through the dev relay, or tests).

import (
	"bufio"
	"compress/gzip"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

// recordUpstream polls base's area endpoint for lat, lon, rangeNM every interval until d has passed (0: until killed)
// and appends each answer as one line to out (gzip). A failed poll is logged and skipped.
func recordUpstream(ctx context.Context, out, base string, lat, lon float64, rangeNM int, interval, d time.Duration) error {
	f, err := os.Create(out)
	if err != nil {
		return err
	}
	defer f.Close()
	zw := gzip.NewWriter(f)
	defer zw.Close()
	client := &http.Client{Timeout: 10 * time.Second}
	url := fmt.Sprintf("%s/api/v3/lat/%.4f/lon/%.4f/dist/%d", base, lat, lon, rangeNM)
	start := time.Now()
	n := 0
	for tick := time.NewTicker(interval); d == 0 || time.Since(start) < d; <-tick.C {
		body, err := getBody(ctx, client, url)
		if err == nil {
			var up upstreamResp // check it's an answer before keeping it
			if err = json.Unmarshal(body, &up); err == nil {
				if _, err = zw.Write(append(compactJSON(body), '\n')); err != nil {
					return err
				}
				if n++; n%60 == 0 {
					zw.Flush()
					log.Printf("record: %d snapshots, %d aircraft in the last", n, len(up.AC))
				}
			}
		}
		if err != nil {
			log.Printf("record: %v", err)
		}
	}
	log.Printf("record: %s: %d snapshots", out, n)
	return nil
}

func getBody(ctx context.Context, client *http.Client, url string) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", userAgent)
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("%s: %s", url, resp.Status)
	}
	return io.ReadAll(resp.Body)
}

// compactJSON drops insignificant whitespace (one snapshot must be one line).
func compactJSON(b []byte) []byte {
	var m json.RawMessage
	if json.Unmarshal(b, &m) != nil {
		return b
	}
	out, err := json.Marshal(m)
	if err != nil {
		return b
	}
	return out
}

// snapshot is one recorded upstream answer: its time, and each aircraft's JSON as recorded (keyed by hex).
type snapshot struct {
	now float64 // ms since epoch, as recorded
	ac  []json.RawMessage
	hex []string
}

// loadRecording reads a -record file (gzip, one snapshot per line).
func loadRecording(path string) ([]snapshot, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	zr, err := gzip.NewReader(f)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	var snaps []snapshot
	sc := bufio.NewScanner(zr)
	sc.Buffer(make([]byte, 1<<20), 16<<20)
	for sc.Scan() {
		var raw struct {
			Now float64           `json:"now"`
			AC  []json.RawMessage `json:"ac"`
		}
		if err := json.Unmarshal(sc.Bytes(), &raw); err != nil {
			return nil, fmt.Errorf("%s line %d: %w", path, len(snaps)+1, err)
		}
		s := snapshot{now: raw.Now, ac: raw.AC}
		for _, a := range raw.AC {
			var h struct {
				Hex string `json:"hex"`
			}
			json.Unmarshal(a, &h)
			s.hex = append(s.hex, strings.ToLower(strings.TrimPrefix(h.Hex, "~")))
		}
		snaps = append(snaps, s)
	}
	if err := sc.Err(); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	if len(snaps) == 0 {
		return nil, fmt.Errorf("%s: no snapshots", path)
	}
	return snaps, nil
}

// replay serves a recording as an adsb.fi-style upstream: the snapshot for the time since start (plus from),
// looping, with "now" rewritten to the wall clock so ages and the logbook's clock work as live.
type replay struct {
	snaps []snapshot
	from  float64 // ms into the recording at start
	start time.Time
	clock func() time.Time

	mu      sync.Mutex
	lastIdx int
}

func newReplay(snaps []snapshot, from time.Duration) *replay {
	return &replay{snaps: snaps, from: float64(from.Milliseconds()), start: time.Now(), clock: time.Now, lastIdx: -1}
}

// current is the snapshot playing now.
func (rp *replay) current() *snapshot {
	first, last := rp.snaps[0].now, rp.snaps[len(rp.snaps)-1].now
	span := last - first + 1000 // the last snapshot plays for a second, then it loops
	t := rp.from + float64(rp.clock().Sub(rp.start).Milliseconds())
	for t >= span {
		t -= span
	}
	i := 0
	for i+1 < len(rp.snaps) && rp.snaps[i+1].now-first <= t {
		i++
	}
	rp.mu.Lock()
	if i < rp.lastIdx {
		log.Printf("replay: looping back to the start")
	}
	rp.lastIdx = i
	rp.mu.Unlock()
	return &rp.snaps[i]
}

func (rp *replay) write(w http.ResponseWriter, ac []json.RawMessage) {
	now := float64(rp.clock().UnixMilli())
	body, _ := json.Marshal(struct {
		Now float64           `json:"now"`
		AC  []json.RawMessage `json:"ac"`
	}{now, ac})
	w.Header().Set("Content-Type", "application/json")
	w.Write(body)
}

func (rp *replay) handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/v3/lat/{lat}/lon/{lon}/dist/{d}", func(w http.ResponseWriter, r *http.Request) {
		s := rp.current()
		ac := s.ac
		if ac == nil {
			ac = []json.RawMessage{}
		}
		rp.write(w, ac) // the relay filters by range itself
	})
	mux.HandleFunc("GET /api/v2/hex/{hex}", func(w http.ResponseWriter, r *http.Request) {
		s := rp.current()
		want := strings.ToLower(r.PathValue("hex"))
		ac := []json.RawMessage{}
		for i, h := range s.hex {
			if h == want {
				ac = append(ac, s.ac[i])
			}
		}
		rp.write(w, ac)
	})
	return mux
}

// serveReplay starts the replay upstream on a loopback port and returns its base URL.
func serveReplay(path string, from time.Duration) (string, error) {
	snaps, err := loadRecording(path)
	if err != nil {
		return "", err
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return "", err
	}
	rp := newReplay(snaps, from)
	go http.Serve(ln, rp.handler())
	secs := (snaps[len(snaps)-1].now - snaps[0].now) / 1000
	log.Printf("replay: %s: %d snapshots over %ss, from %v, looping", path, len(snaps),
		strconv.FormatFloat(secs, 'f', 0, 64), from)
	return "http://" + ln.Addr().String(), nil
}

// readUpstreams reads upstream answers for the -pack modes: a JSON file is one, a recording (.jsonl.gz, -record)
// is one per line. names labels each for logs.
func readUpstreams(ins []string) ([]upstreamResp, []string, error) {
	var ups []upstreamResp
	var names []string
	for _, in := range ins {
		if strings.HasSuffix(in, ".jsonl.gz") {
			snaps, err := loadRecording(in)
			if err != nil {
				return nil, nil, err
			}
			for i, s := range snaps {
				up := upstreamResp{Now: s.now}
				for _, a := range s.ac {
					var ac upstreamAC
					if err := json.Unmarshal(a, &ac); err != nil {
						return nil, nil, fmt.Errorf("%s line %d: %w", in, i+1, err)
					}
					up.AC = append(up.AC, ac)
				}
				ups = append(ups, up)
				names = append(names, fmt.Sprintf("%s:%d", in, i+1))
			}
			continue
		}
		data, err := os.ReadFile(in)
		if err != nil {
			return nil, nil, err
		}
		var up upstreamResp
		if err := json.Unmarshal(data, &up); err != nil {
			return nil, nil, fmt.Errorf("%s: %w", in, err)
		}
		ups = append(ups, up)
		names = append(names, in)
	}
	return ups, names, nil
}

// cutRecording writes the part of a recording from `from` for `d` (0: to the end), keeping only the aircraft in
// hexes when any are given: a test arrival from a long capture.
func cutRecording(out, in string, from, d time.Duration, hexes []string) error {
	snaps, err := loadRecording(in)
	if err != nil {
		return err
	}
	keep := map[string]bool{}
	for _, h := range hexes {
		keep[strings.ToLower(strings.TrimPrefix(h, "~"))] = true
	}
	f, err := os.Create(out)
	if err != nil {
		return err
	}
	defer f.Close()
	zw := gzip.NewWriter(f)
	t0, n := snaps[0].now, 0
	for _, s := range snaps {
		t := time.Duration(s.now-t0) * time.Millisecond
		if t < from || (d > 0 && t >= from+d) {
			continue
		}
		ac := []json.RawMessage{}
		for i, a := range s.ac {
			if len(keep) == 0 || keep[s.hex[i]] {
				ac = append(ac, a)
			}
		}
		line, _ := json.Marshal(struct {
			Now float64           `json:"now"`
			AC  []json.RawMessage `json:"ac"`
		}{s.now, ac})
		if _, err := zw.Write(append(line, '\n')); err != nil {
			return err
		}
		n++
	}
	if err := zw.Close(); err != nil {
		return err
	}
	log.Printf("cut: %s: %d snapshots", out, n)
	return nil
}
