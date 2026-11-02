package main

// Spotter's logbook (lode/relay/logbook.md): the relay logs the traffic around every home a cart has asked about,
// all the time it runs, into SQLite, and serves today's summary (local midnight at the home airport) as /v1/log.
// A rare aircraft (military, adsb.fi's "interesting" flag, or a type on rareTypes) entering range raises a notice
// through the flyover ring, once per aircraft per day. Aircraft with PIA or LADD flags are never logged.

import (
	"context"
	"database/sql"
	_ "embed"
	"encoding/binary"
	"fmt"
	"log"
	"math"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
	_ "time/tzdata" // the relay image has no zoneinfo

	_ "modernc.org/sqlite"
)

const (
	logPath    = "/v1/log"
	logHdr     = 16 // AR_LOG_HDR
	logRecs    = 3  // AR_LOG_RECS: fastest, highest, longest
	logRare    = 8  // AR_LOG_RARE
	logEntry   = 16
	logSize    = logHdr + logEntry*(logRecs+logRare) // AR_LOG_SIZE
	lNew       = 0x01                                // AR_L_NEW: nothing logged yet today
	rMil       = 0x01                                // AR_R_*: why a sighting is rare
	rFlagged   = 0x02
	rType      = 0x04
	logRangeNM = 25               // the logbook's area around home
	logEvery   = 10 * time.Second // background poll per home
	logForget  = 30 * 24 * time.Hour

	dbMil, dbInteresting, dbPIA, dbLADD = 1, 2, 4, 8 // adsb.fi dbFlags
)

// rareTypes: unusual at a regional airport (ICAO type designators).
var rareTypes = map[string]bool{
	"A388": true, "A124": true, "A225": true, "A3ST": true, "A337": true, "B748": true, "BLCF": true, "MD11": true,
	"DC10": true, "IL76": true, "AN12": true, "A400": true, "C5M": true, "C17": true, "B52": true, "B1": true,
	"B2": true, "U2": true, "E3TF": true, "E3CF": true, "E6": true, "P51": true, "B17": true, "B25": true,
	"B29": true, "SPIT": true, "CONC": true,
}

//go:embed airport_tz.csv
var airportTZCSV string

type aptZone struct {
	icao     string
	lat, lon float64
	tz       string
}

var airportZones = func() []aptZone {
	var zs []aptZone
	for _, line := range strings.Split(airportTZCSV, "\n") {
		f := strings.Split(line, ",")
		if len(f) != 4 || strings.HasPrefix(line, "#") {
			continue
		}
		lat, e1 := strconv.ParseFloat(f[1], 64)
		lon, e2 := strconv.ParseFloat(f[2], 64)
		if e1 == nil && e2 == nil {
			zs = append(zs, aptZone{f[0], lat, lon, f[3]})
		}
	}
	return zs
}()

// homeZone is the time zone of the airport nearest a home point (UTC if none loads).
func homeZone(lat, lon float64) (string, *time.Location) {
	best, bd := aptZone{tz: "UTC"}, math.Inf(1)
	for _, z := range airportZones {
		if d, _ := distBearingNM(lat, lon, z.lat, z.lon); d < bd {
			best, bd = z, d
		}
	}
	loc, err := time.LoadLocation(best.tz)
	if err != nil {
		return best.icao, time.UTC
	}
	return best.icao, loc
}

type logHome struct {
	key      string
	lat, lon float64
	airport  string
	loc      *time.Location
	saved    time.Time // homes.last_seen last written
	started  time.Time // this process began logging it
}

type logbook struct {
	db  *sql.DB
	src *source
	ns  *notices
	now func() time.Time

	mu    sync.Mutex
	homes map[string]*logHome
}

// openLogbook opens (or creates) the DB at path; "" keeps it in memory (a restart starts afresh).
func openLogbook(path string, src *source, ns *notices) (*logbook, error) {
	dsn := path // a file name, or a "file:" URI (tests: named in-memory DBs)
	if dsn == "" {
		dsn = "file:relaylog?mode=memory&cache=shared"
	}
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1) // one writer; the poller and handlers take turns
	for _, q := range []string{
		`PRAGMA journal_mode=WAL`,
		`CREATE TABLE IF NOT EXISTS homes (key TEXT PRIMARY KEY, lat REAL, lon REAL, last_seen INTEGER)`,
		`CREATE TABLE IF NOT EXISTS sightings (home TEXT, day TEXT, hex TEXT, cs TEXT, type TEXT, first INTEGER,
			last INTEGER, max_alt INTEGER, max_gs INTEGER, rare INTEGER, notified INTEGER DEFAULT 0,
			PRIMARY KEY (home, day, hex))`,
	} {
		if _, err := db.Exec(q); err != nil && !strings.HasPrefix(q, "PRAGMA") {
			return nil, fmt.Errorf("logbook %s: %w", path, err)
		}
	}
	lb := &logbook{db: db, src: src, ns: ns, now: time.Now, homes: map[string]*logHome{}}
	rows, err := db.Query(`SELECT key, lat, lon FROM homes WHERE last_seen > ?`, lb.now().Add(-logForget).Unix())
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	for rows.Next() {
		var h logHome
		if rows.Scan(&h.key, &h.lat, &h.lon) == nil {
			lb.addHome(&h)
		}
	}
	return lb, nil
}

func homeKey(lat, lon float64) string { return fmt.Sprintf("%.4f,%.4f", lat, lon) }

func (lb *logbook) addHome(h *logHome) *logHome {
	h.airport, h.loc = homeZone(h.lat, h.lon)
	h.started = lb.now()
	lb.homes[h.key] = h
	log.Printf("logbook: logging %s (%s, %s)", h.key, h.airport, h.loc)
	return h
}

// seen notes a cart's home (each frame request); the DB row is refreshed at most hourly.
func (lb *logbook) seen(lat, lon float64) *logHome {
	if lb == nil {
		return nil
	}
	key := homeKey(lat, lon)
	lb.mu.Lock()
	h := lb.homes[key]
	if h == nil {
		h = lb.addHome(&logHome{key: key, lat: lat, lon: lon})
	}
	now := lb.now()
	write := now.Sub(h.saved) > time.Hour
	if write {
		h.saved = now
	}
	lb.mu.Unlock()
	if write {
		if _, err := lb.db.Exec(`INSERT INTO homes (key, lat, lon, last_seen) VALUES (?, ?, ?, ?)
			ON CONFLICT (key) DO UPDATE SET last_seen = excluded.last_seen`, key, lat, lon, now.Unix()); err != nil {
			log.Printf("logbook: %v", err)
		}
	}
	return h
}

// run polls every home every logEvery, through the source's rate limit, until ctx ends.
func (lb *logbook) run(ctx context.Context) {
	t := time.NewTicker(logEvery)
	defer t.Stop()
	for {
		lb.mu.Lock()
		var hs []*logHome
		for _, h := range lb.homes {
			if lb.now().Sub(h.saved) < logForget || h.saved.IsZero() {
				hs = append(hs, h)
			}
		}
		lb.mu.Unlock()
		for _, h := range hs {
			up, err := lb.src.get(ctx, fmt.Sprintf("%s/api/v3/lat/%.4f/lon/%.4f/dist/%d", lb.src.baseURL, h.lat, h.lon, logRangeNM))
			if err != nil {
				log.Printf("logbook %s: %v", h.key, err)
				continue
			}
			lb.record(h, up)
		}
		select {
		case <-ctx.Done():
			return
		case <-t.C:
		}
	}
}

func dayOf(t time.Time, loc *time.Location) string { return t.In(loc).Format("2006-01-02") }

// record adds one snapshot to today's sightings, then raises a notice for each rare aircraft not yet announced.
func (lb *logbook) record(h *logHome, up *upstreamResp) {
	now := lb.now()
	day := dayOf(now, h.loc)
	tx, err := lb.db.Begin()
	if err != nil {
		log.Printf("logbook: %v", err)
		return
	}
	defer tx.Rollback()
	for i := range up.AC {
		a := &up.AC[i]
		if a.Lat == nil || a.Lon == nil || a.DBFlags&(dbPIA|dbLADD) != 0 {
			continue
		}
		if d, _ := distBearingNM(h.lat, h.lon, *a.Lat, *a.Lon); d > logRangeNM {
			continue
		}
		hex := strings.ToLower(strings.TrimPrefix(a.Hex, "~"))
		alt := 0
		if v, ok := a.AltBaro.(float64); ok {
			alt = int(v)
		}
		gs := 0
		if a.GS != nil {
			gs = int(math.Round(*a.GS))
		}
		rare := 0
		if a.DBFlags&dbMil != 0 {
			rare |= rMil
		}
		if a.DBFlags&dbInteresting != 0 {
			rare |= rFlagged
		}
		if rareTypes[strings.ToUpper(a.Type)] {
			rare |= rType
		}
		cs := strings.TrimSpace(a.Flight)
		if cs == "" {
			cs = strings.TrimSpace(a.Reg)
		}
		if _, err := tx.Exec(`INSERT INTO sightings (home, day, hex, cs, type, first, last, max_alt, max_gs, rare)
			VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
			ON CONFLICT (home, day, hex) DO UPDATE SET last = excluded.last,
				cs = CASE WHEN excluded.cs <> '' THEN excluded.cs ELSE sightings.cs END,
				type = CASE WHEN excluded.type <> '' THEN excluded.type ELSE sightings.type END,
				max_alt = max(sightings.max_alt, excluded.max_alt), max_gs = max(sightings.max_gs, excluded.max_gs),
				rare = sightings.rare | excluded.rare`,
			h.key, day, hex, cs, strings.ToUpper(a.Type), now.Unix(), now.Unix(), alt, gs, rare); err != nil {
			log.Printf("logbook: %v", err)
			return
		}
	}
	if err := tx.Commit(); err != nil {
		log.Printf("logbook: %v", err)
		return
	}
	lb.announce(h, day, up)
}

// announce turns today's unannounced rare sightings into notices (once per aircraft per day).
func (lb *logbook) announce(h *logHome, day string, up *upstreamResp) {
	rows, err := lb.db.Query(`SELECT hex FROM sightings WHERE home = ? AND day = ? AND rare <> 0 AND notified = 0`, h.key, day)
	if err != nil {
		return
	}
	var hexes []string
	for rows.Next() {
		var x string
		if rows.Scan(&x) == nil {
			hexes = append(hexes, x)
		}
	}
	rows.Close()
	for _, x := range hexes {
		lb.db.Exec(`UPDATE sightings SET notified = 1 WHERE home = ? AND day = ? AND hex = ?`, h.key, day, x)
		for i := range up.AC {
			a := &up.AC[i]
			if strings.ToLower(strings.TrimPrefix(a.Hex, "~")) == x && a.Lat != nil && a.Lon != nil {
				n := lb.ns.push(rareNotice(h, a))
				log.Printf("notice %d (rare %s): %s", n.id, x, n.text())
				break
			}
		}
	}
}

var compass8 = []string{"N", "NE", "E", "SE", "S", "SW", "W", "NW"}

// rareNotice: "RARE!  RCH4521" / the type's description / "12 NM SW  3,500FT".
func rareNotice(h *logHome, a *upstreamAC) notice {
	n := notice{flags: nICAO, secs: noticeSecs}
	if v, err := strconv.ParseUint(strings.TrimPrefix(a.Hex, "~"), 16, 24); err == nil {
		n.icao = uint32(v)
	}
	cs := strings.TrimSpace(a.Flight)
	if cs == "" {
		cs = strings.TrimSpace(a.Reg)
	}
	head := "RARE!"
	if a.DBFlags&dbMil != 0 {
		head = "MILITARY!"
	}
	padASCII(n.lines[0][:], strings.ToUpper(fmt.Sprintf("%-10s %s", head, cs)))
	what := strings.TrimSpace(a.Desc)
	if what == "" {
		what = a.Type
	}
	padASCII(n.lines[1][:], strings.ToUpper(what))
	d, brg := distBearingNM(h.lat, h.lon, *a.Lat, *a.Lon)
	pt := compass8[int(math.Round(math.Mod(brg*180/math.Pi+360, 360)/45))%8]
	l3 := fmt.Sprintf("%.0f NM %s", d, pt)
	if v, ok := a.AltBaro.(float64); ok {
		l3 += "  " + thousands(int(v)) + "FT"
	}
	padASCII(n.lines[2][:], l3)
	return n
}

type logEntryRec struct {
	cs, typ string
	value   int
	minute  int // rare: minute of the local day first seen
	rare    int
}

type logSummary struct {
	flags           uint8
	month, mday     int
	aircraft, types int
	peakHour, peakN int
	since           int // minute of the local day logging began, 0: since midnight
	recs            [logRecs]logEntryRec
	rares           []logEntryRec
}

// summary is today's logbook for a home (registering it if new).
func (lb *logbook) summary(lat, lon float64) (logSummary, error) {
	h := lb.seen(lat, lon)
	now := lb.now().In(h.loc)
	day := now.Format("2006-01-02")
	s := logSummary{month: int(now.Month()), mday: now.Day()}
	midnight := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, h.loc)
	if st := h.started.In(h.loc); st.After(midnight) {
		s.since = st.Hour()*60 + st.Minute()
	}
	var hours [24]int
	rows, err := lb.db.Query(`SELECT cs, type, first, last, max_alt, max_gs, rare FROM sightings WHERE home = ? AND day = ?`, h.key, day)
	if err != nil {
		return s, err
	}
	defer rows.Close()
	types := map[string]bool{}
	for rows.Next() {
		var e logEntryRec
		var first, last int64
		var alt, gs int
		if err := rows.Scan(&e.cs, &e.typ, &first, &last, &alt, &gs, &e.rare); err != nil {
			return s, err
		}
		s.aircraft++
		if e.typ != "" {
			types[e.typ] = true
		}
		ft := time.Unix(first, 0).In(h.loc)
		hours[ft.Hour()]++
		for i, v := range []int{gs, alt / 100, int(last-first) / 60} {
			if v > s.recs[i].value {
				s.recs[i] = logEntryRec{cs: e.cs, typ: e.typ, value: v}
			}
		}
		if e.rare != 0 {
			e.minute = ft.Hour()*60 + ft.Minute()
			s.rares = append(s.rares, e)
		}
	}
	s.types = len(types)
	for hr, n := range hours {
		if n > s.peakN {
			s.peakHour, s.peakN = hr, n
		}
	}
	sort.Slice(s.rares, func(i, j int) bool { return s.rares[i].minute > s.rares[j].minute }) // newest first
	if len(s.rares) > logRare {
		s.rares = s.rares[:logRare]
	}
	if s.aircraft == 0 {
		s.flags |= lNew
	}
	return s, rows.Err()
}

// encode writes struct ar_log, then AR_LOG_RECS struct ar_log_rec, then AR_LOG_RARE struct ar_log_rare.
func (s *logSummary) encode() []byte {
	b := make([]byte, logSize)
	b[0], b[1], b[2], b[3] = 'A', 'L', frameVer, s.flags
	b[4], b[5] = byte(s.month), byte(s.mday)
	binary.BigEndian.PutUint16(b[6:], uint16(min(s.aircraft, 0xFFFF)))
	binary.BigEndian.PutUint16(b[8:], uint16(min(s.types, 0xFFFF)))
	b[10], b[11], b[12] = byte(s.peakHour), byte(min(s.peakN, 255)), byte(len(s.rares))
	binary.BigEndian.PutUint16(b[14:], uint16(s.since))
	put := func(o int, e logEntryRec, rare bool) {
		padASCII(b[o:o+8], strings.ToUpper(e.cs))
		padASCII(b[o+8:o+12], strings.ToUpper(e.typ))
		if rare {
			binary.BigEndian.PutUint16(b[o+12:], uint16(e.minute))
			b[o+14] = byte(e.rare)
		} else {
			binary.BigEndian.PutUint16(b[o+12:], uint16(min(e.value, 0xFFFF)))
		}
	}
	for i, e := range s.recs {
		put(logHdr+i*logEntry, e, false)
	}
	for i := 0; i < logRare; i++ {
		e := logEntryRec{}
		if i < len(s.rares) {
			e = s.rares[i]
		}
		put(logHdr+(logRecs+i)*logEntry, e, true)
	}
	return b
}

func logHandler(lb *logbook) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !versionOK(w, r) {
			return
		}
		q, err := parseQuery(r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		s, err := lb.summary(q.lat, q.lon)
		if err != nil {
			log.Printf("%s %s: %v", r.RemoteAddr, r.URL.RequestURI(), err)
			http.Error(w, "logbook unavailable", http.StatusInternalServerError)
			return
		}
		writeBinary(w, s.encode())
		log.Printf("%s %s -> %d aircraft, %d types, %d rare", r.RemoteAddr, r.URL.RequestURI(), s.aircraft, s.types, len(s.rares))
	}
}
