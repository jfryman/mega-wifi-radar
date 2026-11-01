package main

// Flyover notices from the owner's atc-mqtt service (lode/plans/flyover-notices.md): flights entering its
// geofence become short three-line notices the cart can show. The relay keeps the newest few for a TTL and
// nothing per cart; the cart asks for anything after the last id it saw.

import (
	"crypto/rand"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"hash/fnv"
	"log"
	"math"
	"net/http"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	noticePath    = "/v1/notice"
	noticeSize    = 88               // AR_NOTICE_SIZE
	noticeKeep    = 4                // notices kept
	noticeCols    = 26               // AR_NOTICE_COLS: characters per line (the cart's banner width)
	noticeSecs    = 15               // how long the cart shows one
	noticeMaxAge  = 90 * time.Second // flights reported longer ago than this are dropped (a backlog, a slow broker)
	matchMaxAge   = 15 * time.Second // the adsb.fi snapshot must be this fresh to match against
	matchMaxNM    = 1.5              // after projecting the FR24 position to the snapshot's time
	matchMaxFt    = 2000
	matchCSMaxNM  = 10 // a callsign match may be further off (FR24 positions lag up to a poll interval)
	nICAO         = 0x01
	nEmerg        = 0x02
	seenUUIDsKeep = time.Hour
)

// atcFlight is one flight object from atc-mqtt (its INTERFACE.md). Missing strings arrive as "Unknown".
// flight_number is FR24's callsign, which is the ADS-B one (SWA1234); aircraft_type is FR24's aircraft code,
// normally an ICAO type designator (B738), sometimes a full name.
type atcFlight struct {
	UUID        string  `json:"flight_uuid"`
	Number      string  `json:"flight_number"`
	Airline     string  `json:"airline"`
	Type        string  `json:"aircraft_type"`
	Origin      string  `json:"origin"`
	Destination string  `json:"destination"`
	Heading     float64 `json:"heading"`
	AltFt       float64 `json:"altitude_feet"`
	Lat         float64 `json:"latitude"`
	Lon         float64 `json:"longitude"`
	GsKt        float64 `json:"ground_speed_knots"`
	Timestamp   string  `json:"timestamp"`
	Event       string  `json:"event_type"`
}

// parseATC returns the flights entering the geofence. It takes the consolidated atc/status message
// ({entering_flights: [...], ...}) or a single flight object (the legacy atc/incoming topic).
func parseATC(payload []byte) ([]atcFlight, error) {
	var msg struct {
		Entering *[]atcFlight `json:"entering_flights"`
		Error    string       `json:"error"`
		atcFlight
	}
	if err := json.Unmarshal(payload, &msg); err != nil {
		return nil, err
	}
	switch {
	case msg.Entering != nil:
		if msg.Error != "" {
			return nil, errors.New("atc-mqtt reports: " + msg.Error)
		}
		return *msg.Entering, nil
	case msg.UUID != "" && msg.Event != "exiting":
		return []atcFlight{msg.atcFlight}, nil
	}
	return nil, nil
}

type notice struct {
	id    uint16 // never 0: the cart's "none seen yet"
	at    time.Time
	icao  uint32
	flags uint8 // nICAO, nEmerg
	secs  uint8
	lines [3][noticeCols]byte
}

// encode writes struct ar_notice (src/arframe.h). The zero notice (id 0) means "none waiting".
func (n *notice) encode() []byte {
	b := make([]byte, noticeSize)
	b[0], b[1], b[2], b[3] = 'A', 'N', frameVer, n.flags
	binary.BigEndian.PutUint16(b[4:], n.id)
	b[6], b[7], b[8], b[9] = byte(n.icao>>16), byte(n.icao>>8), byte(n.icao), n.secs
	for i := range n.lines {
		if n.id == 0 {
			padASCII(b[10+i*noticeCols:10+(i+1)*noticeCols], "")
		} else {
			copy(b[10+i*noticeCols:], n.lines[i][:])
		}
	}
	return b
}

func (n *notice) text() string {
	return fmt.Sprintf("%s|%s|%s", n.lines[0][:], n.lines[1][:], n.lines[2][:])
}

// notices is the relay's notice ring.
type notices struct {
	src *source // for the ICAO match; nil in tests that don't need one
	ttl time.Duration
	now func() time.Time

	mu   sync.Mutex
	ring []notice
	next uint16
	seen map[string]time.Time // flight_uuid -> when (QoS 1 can redeliver; atc-mqtt repeats nothing else)
}

func newNotices(src *source, ttl time.Duration) *notices {
	var b [2]byte
	rand.Read(b[:]) // a restarted relay mustn't reuse the ids a cart remembers
	return &notices{src: src, ttl: ttl, now: time.Now, next: binary.BigEndian.Uint16(b[:]), seen: map[string]time.Time{}}
}

// handle is the MQTT callback.
func (ns *notices) handle(m mqttMessage) {
	flights, err := parseATC(m.payload)
	if err != nil {
		log.Printf("mqtt: %s: %v", m.topic, err)
		return
	}
	for _, f := range flights {
		if n, ok := ns.add(f); ok {
			icao := "no match"
			if n.flags&nICAO != 0 {
				icao = fmt.Sprintf("icao %06x", n.icao)
			}
			log.Printf("notice %d (%s): %s", n.id, icao, n.text())
		}
	}
}

// add turns one entering flight into a notice. It reports false for a duplicate or a stale report.
func (ns *notices) add(f atcFlight) (notice, bool) {
	now := ns.now()
	if t, err := time.Parse(time.RFC3339, f.Timestamp); err == nil && now.Sub(t) > noticeMaxAge {
		log.Printf("notice: %s reported %v ago: dropped", f.Number, now.Sub(t).Round(time.Second))
		return notice{}, false
	}
	var ac *upstreamAC
	if ns.src != nil {
		ac = ns.src.match(f, now)
	}

	ns.mu.Lock()
	defer ns.mu.Unlock()
	for u, at := range ns.seen {
		if now.Sub(at) > seenUUIDsKeep {
			delete(ns.seen, u)
		}
	}
	if f.UUID != "" {
		if _, dup := ns.seen[f.UUID]; dup {
			return notice{}, false
		}
		ns.seen[f.UUID] = now
	}
	n := composeNotice(f, ac)
	ns.next++
	if ns.next == 0 {
		ns.next = 1
	}
	n.id, n.at = ns.next, now
	ns.ring = append(ns.ring, n)
	if len(ns.ring) > noticeKeep {
		ns.ring = ns.ring[len(ns.ring)-noticeKeep:]
	}
	return n, true
}

// waiting reports whether a notice newer than the request's n= is live (the AR_H_NOTICE / AR_K_NOTICE flag).
// A nil ring (tests) or a missing n= never waits.
func (ns *notices) waiting(r *http.Request) bool {
	if ns == nil {
		return false
	}
	id, err := strconv.ParseUint(r.URL.Query().Get("n"), 10, 16)
	if err != nil {
		return false
	}
	_, ok := ns.after(uint16(id))
	return ok
}

// noticeHTTP serves GET /v1/notice?after=<id>&v=: the oldest live notice newer than id, or the zero notice.
func noticeHTTP(ns *notices) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !versionOK(w, r) {
			return
		}
		id, err := strconv.ParseUint(r.URL.Query().Get("after"), 10, 16)
		if err != nil {
			http.Error(w, "after must be a notice id (0-65535)", http.StatusBadRequest)
			return
		}
		n, _ := ns.after(uint16(id))
		writeBinary(w, n.encode())
		log.Printf("%s %s -> notice %d", r.RemoteAddr, r.URL.RequestURI(), n.id)
	}
}

// after returns the oldest live notice newer than id (0: none seen yet, so any live notice).
// Ids wrap, so "newer" is a signed 16-bit difference.
func (ns *notices) after(id uint16) (notice, bool) {
	if ns == nil {
		return notice{}, false
	}
	ns.mu.Lock()
	defer ns.mu.Unlock()
	now := ns.now()
	for _, n := range ns.ring {
		if now.Sub(n.at) <= ns.ttl && (id == 0 || int16(n.id-id) > 0) {
			return n, true
		}
	}
	return notice{}, false
}

// match finds the adsb.fi aircraft a notice is about: the same callsign within matchCSMaxNM, else the
// nearest within matchMaxNM / matchMaxFt, after moving the FR24 position along its heading to the
// snapshot's time. Nil if the snapshot is too old (no cart polling) or nothing fits.
func (s *source) match(f atcFlight, now time.Time) *upstreamAC {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.last == nil || now.Sub(s.lastAt) > matchMaxAge || (f.Lat == 0 && f.Lon == 0) {
		return nil
	}
	lat, lon := f.Lat, f.Lon
	if t, err := time.Parse(time.RFC3339, f.Timestamp); err == nil && s.last.Now > 0 {
		dt := time.UnixMilli(int64(s.last.Now)).Sub(t).Hours()
		if dt > 0 && dt < 0.05 { // at most 3 minutes of dead reckoning
			lat, lon = project(lat, lon, f.Heading, f.GsKt*dt)
		}
	}
	cs := strings.ToUpper(strings.TrimSpace(f.Number))
	var best *upstreamAC
	bestD := math.Inf(1)
	for i := range s.last.AC {
		a := &s.last.AC[i]
		if a.Lat == nil || a.Lon == nil {
			continue
		}
		d, _ := distBearingNM(lat, lon, *a.Lat, *a.Lon)
		if cs != "" && strings.ToUpper(strings.TrimSpace(a.Flight)) == cs && d <= matchCSMaxNM {
			c := *a
			return &c
		}
		if alt, ok := a.AltBaro.(float64); ok && f.AltFt > 0 && math.Abs(alt-f.AltFt) > matchMaxFt {
			continue
		}
		if d <= matchMaxNM && d < bestD {
			best, bestD = a, d
		}
	}
	if best == nil {
		return nil
	}
	c := *best
	return &c
}

// project moves a position distNM along a true heading (flat earth: a few nm at most).
func project(lat, lon, headingDeg, distNM float64) (float64, float64) {
	h := headingDeg * math.Pi / 180
	dlat := distNM * math.Cos(h) / 60
	dlon := distNM * math.Sin(h) / (60 * math.Cos(lat*math.Pi/180))
	return lat + dlat, lon + dlon
}

var noticeHeadlines = []string{"OVERHEAD!", "LOOK UP!", "INCOMING!", "FLYBY!"}

// composeNotice writes the three lines (upper-case ASCII, noticeCols each):
//
//	OVERHEAD!  SWA1234
//	SOUTHWEST   B738
//	BNA > MDW  1,800FT 160KT
func composeNotice(f atcFlight, ac *upstreamAC) notice {
	n := notice{secs: noticeSecs}
	cs := known(f.Number)
	if ac != nil {
		n.flags |= nICAO
		fmt.Sscanf(strings.TrimPrefix(ac.Hex, "~"), "%x", &n.icao)
		if r, ok := convert(ac, 0, 0, math.Inf(1)); ok && r.flags&fEmerg != 0 {
			n.flags |= nEmerg
		}
		if s := strings.TrimSpace(ac.Flight); s != "" {
			cs = s // what the radar's tag and traffic list show
		}
	}

	h := fnv.New32a()
	h.Write([]byte(f.UUID + cs))
	head := noticeHeadlines[h.Sum32()%uint32(len(noticeHeadlines))]
	if n.flags&nEmerg != 0 {
		head = "MAYDAY!"
	}
	if cs == "" {
		cs = "MYSTERY PLANE"
	}
	line := [3]string{fmt.Sprintf("%-10s %s", head, cs)}

	airline := shortAirline(f.Airline)
	if airline == "" {
		airline = airlineFromCallsign(cs)
	}
	typ := shortType(f.Type)
	if typ == "" && ac != nil {
		typ = strings.TrimSpace(ac.Type)
	}
	switch {
	case airline != "" && typ != "":
		line[1] = fmt.Sprintf("%-12s%s", airline, typ)
	default:
		line[1] = airline + typ
	}

	var route string
	o, d := shortAirport(f.Origin), shortAirport(f.Destination)
	switch {
	case o != "" && d != "":
		route = o + " > " + d
	case o != "":
		route = "FROM " + o
	case d != "":
		route = "TO " + d
	}
	var nums []string
	if f.AltFt > 0 {
		nums = append(nums, thousands(int(math.Round(f.AltFt)))+"FT")
	}
	if f.GsKt > 0 {
		nums = append(nums, fmt.Sprintf("%dKT", int(math.Round(f.GsKt))))
	}
	line[2] = strings.TrimSpace(fmt.Sprintf("%-10s %s", route, strings.Join(nums, " ")))
	for len(line[2]) > noticeCols && len(nums) > 0 { // drop the speed, then the altitude, to fit
		nums = nums[:len(nums)-1]
		line[2] = strings.TrimSpace(fmt.Sprintf("%-10s %s", route, strings.Join(nums, " ")))
	}

	for i := range line {
		padASCII(n.lines[i][:], strings.ToUpper(line[i]))
	}
	return n
}

// known blanks atc-mqtt's "Unknown" placeholder.
func known(s string) string {
	s = strings.TrimSpace(s)
	if strings.EqualFold(s, "unknown") || strings.EqualFold(s, "n/a") {
		return ""
	}
	return s
}

func shortAirport(s string) string {
	s = known(s)
	if len(s) > 4 { // a name, not a code: its first word
		s = strings.Fields(s)[0]
		if len(s) > 10 {
			s = s[:10]
		}
	}
	return strings.ToUpper(s)
}

// shortAirline drops the "Airlines"-type words: "Delta Air Lines" -> DELTA, "Air Canada" stays.
func shortAirline(s string) string {
	s = strings.ToUpper(known(s))
	var keep []string
	for _, w := range strings.Fields(s) {
		switch w {
		case "AIRLINES", "AIRLINE", "AIRWAYS", "LINES", "INC", "INC.", "CO", "CO.", "LLC", "CORP", "INTERNATIONAL":
			continue
		}
		keep = append(keep, w)
	}
	if len(keep) > 1 && keep[len(keep)-1] == "AIR" && keep[0] != "AIR" { // "Delta Air (Lines)"
		keep = keep[:len(keep)-1]
	}
	out := strings.Join(keep, " ")
	if len(out) > 11 && len(keep) > 0 {
		out = keep[0]
		if len(out) > 11 {
			out = out[:11]
		}
	}
	return out
}

// shortType keeps an ICAO type code as it is and shortens a full name: "Boeing 737-800" -> B737-800.
func shortType(s string) string {
	s = strings.ToUpper(known(s))
	f := strings.Fields(s)
	if len(f) < 2 {
		return s
	}
	rest := strings.Join(f[1:], " ")
	switch f[0] {
	case "BOEING", "CESSNA":
		if rest[0] >= '0' && rest[0] <= '9' {
			rest = f[0][:1] + rest
		}
	case "AIRBUS", "EMBRAER", "BOMBARDIER", "MITSUBISHI", "PIPER", "BEECHCRAFT", "DE", "MCDONNELL":
	default:
		rest = s
	}
	if len(rest) > 14 {
		rest = rest[:14]
	}
	return rest
}

// airlineFromCallsign names the operator from an ICAO callsign prefix, for when atc-mqtt has no airline.
func airlineFromCallsign(cs string) string {
	if len(cs) < 4 {
		return ""
	}
	return callsignAirlines[cs[:3]]
}

var callsignAirlines = map[string]string{
	"AAL": "AMERICAN", "ASA": "ALASKA", "AAY": "ALLEGIANT", "DAL": "DELTA", "EDV": "ENDEAVOR", "ENY": "ENVOY",
	"FDX": "FEDEX", "FFT": "FRONTIER", "JBU": "JETBLUE", "JIA": "PSA", "MXY": "BREEZE", "NKS": "SPIRIT",
	"RPA": "REPUBLIC", "SKW": "SKYWEST", "SWA": "SOUTHWEST", "UAL": "UNITED", "UPS": "UPS", "GJS": "GOJET",
	"ASH": "MESA", "QXE": "HORIZON", "ACA": "AIR CANADA", "BAW": "BRITISH", "DLH": "LUFTHANSA", "AFR": "AIR FRANCE",
	"KLM": "KLM", "GTI": "ATLAS", "ABX": "ABX", "CPZ": "COMPASS", "SCX": "SUN COUNTRY", "VXP": "AVELO",
}

// thousands formats 12500 as "12,500".
func thousands(v int) string {
	s := fmt.Sprint(v)
	for i := len(s) - 3; i > 0 && s[i-1] != '-'; i -= 3 {
		s = s[:i] + "," + s[i:]
	}
	return s
}
