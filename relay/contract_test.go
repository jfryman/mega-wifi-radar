package main

// Contract test: src/arframe.h is the single source of truth for what the relay and the cartridge
// agree on (lode/architecture/contract.md). This test parses it and checks the relay against every
// AR_* item: constants, the frame and detail layouts (each field at its AR_AT offset), the HTTP
// paths, queries and version check, and the mDNS discovery strings. Any AR_* item or struct the
// test doesn't know about fails it, so a new contract item can't slip past. contract.lock pins a
// hash of the header to AR_VER, so the header can't change without a version bump.

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"fmt"
	"net"
	"net/http/httptest"
	"os"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"
)

const headerPath = "../src/arframe.h"

type cHeader struct {
	ints    map[string]int64
	strs    map[string]string
	exprs   map[string]string         // non-literal defines, e.g. AR_FRAME_MAX
	fields  map[string]map[string]int // struct -> field -> size in bytes
	order   map[string][]string       // struct -> fields in declaration order
	offsets map[string]map[string]int // struct -> field -> AR_AT offset
}

var (
	reDefine = regexp.MustCompile(`^#define\s+(AR_\w+)\s+(.+?)\s*(?:/\*.*)?$`)
	reStruct = regexp.MustCompile(`^struct\s+(ar_\w+)\s*\{`)
	reField  = regexp.MustCompile(`^\s*(char|u8|s8|u16|s16|u32|s32)\s+(\w+)(?:\[(\d+)\])?;`)
	reAt     = regexp.MustCompile(`AR_AT\((\w+),\s*(\w+),\s*(\d+)\)`)
	cSizes   = map[string]int{"char": 1, "u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4}
)

func loadHeader(t *testing.T) cHeader {
	t.Helper()
	src, err := os.ReadFile(headerPath)
	if err != nil {
		if os.Getenv("CI") != "" {
			t.Fatalf("contract header missing in CI: %v", err)
		}
		t.Skipf("no %s (relay-only build context): contract checked in CI", headerPath)
	}
	h := cHeader{map[string]int64{}, map[string]string{}, map[string]string{},
		map[string]map[string]int{}, map[string][]string{}, map[string]map[string]int{}}
	cur := ""
	for _, line := range strings.Split(string(src), "\n") {
		if m := reDefine.FindStringSubmatch(line); m != nil {
			name, v := m[1], strings.TrimSpace(m[2])
			if n, err := strconv.ParseInt(v, 0, 64); err == nil {
				h.ints[name] = n
			} else if s, err := strconv.Unquote(v); err == nil {
				h.strs[name] = s
			} else {
				h.exprs[name] = v
			}
			continue
		}
		if m := reStruct.FindStringSubmatch(line); m != nil {
			cur = m[1]
			h.fields[cur] = map[string]int{}
			continue
		}
		if cur != "" {
			if strings.HasPrefix(strings.TrimSpace(line), "};") {
				cur = ""
			} else if m := reField.FindStringSubmatch(line); m != nil {
				n := 1
				if m[3] != "" {
					n, _ = strconv.Atoi(m[3])
				}
				h.fields[cur][m[2]] = cSizes[m[1]] * n
				h.order[cur] = append(h.order[cur], m[2])
			}
		}
		for _, m := range reAt.FindAllStringSubmatch(line, -1) {
			if m[1] == "t" { // the macro definition itself
				continue
			}
			if h.offsets[m[1]] == nil {
				h.offsets[m[1]] = map[string]int{}
			}
			h.offsets[m[1]][m[2]], _ = strconv.Atoi(m[3])
		}
	}
	return h
}

// Every AR_* integer constant, and the relay value it must equal.
var relayInts = map[string]int64{
	"AR_VER": frameVer, "AR_REC_SIZE_V1": recSize, "AR_MAX_RECS": maxRecs, "AR_HDR_SIZE": hdrSize,
	"AR_SRC_ADSBFI":  sourceADSBfi,
	"AR_H_TRUNCATED": hTruncated, "AR_H_UPSTREAM_ERR": hUpstreamErr,
	"AR_F_GROUND": fGround, "AR_F_EMERG": fEmerg, "AR_F_STALE": fStale, "AR_F_ALT_UNK": fAltUnk,
	"AR_F_GS_UNK": fGSUnk, "AR_F_TRK_UNK": fTrkUnk, "AR_F_VR_UNK": fVRUnk,
	"AR_SQUAWK_NONE": squawkNone, "AR_CAT_NONE": catNone,
	"AR_HTTP_MISMATCH": httpMismatch, "AR_DETAIL_SIZE": detailSize,
	"AR_D_FOUND": dFound, "AR_D_GEOM_ALT": dGeomAlt, "AR_D_SEL_ALT": dSelAlt, "AR_D_SEL_HDG": dSelHdg,
	"AR_D_QNH": dQNH, "AR_D_YEAR": dYear, "AR_D_SEEN": dSeen,
	"AR_TN": tn, "AR_TERRAIN_HDR": terrainHdr, "AR_LEVELS": levels, "AR_KEY_LEN": keyLen, "AR_T_FLAT": tFlat,
	"AR_TRACK_SIZE": trackSize, "AR_K_FOUND": kFound, "AR_K_GROUND": kGround, "AR_K_EMERG": kEmerg,
	"AR_K_ALT_UNK": kAltUnk, "AR_STRIP_CELLS": stripCells,
}

// Items checked by their own tests below, or reserved values the relay never emits.
var checkedElsewhere = map[string]string{
	"AR_SRC_ADSBLOL":   "reserved source id (relay only emits adsb.fi)",
	"AR_SRC_OPENSKY":   "reserved source id (relay only emits adsb.fi)",
	"AR_FRAME_MAX":     "TestContractConstants (derived)",
	"AR_FRAME_PATH":    "TestContractHTTP",
	"AR_FRAME_QUERY":   "TestContractHTTP",
	"AR_DETAIL_PATH":   "TestContractHTTP",
	"AR_DETAIL_QUERY":  "TestContractHTTP",
	"AR_TERRAIN_PATH":  "TestContractHTTP",
	"AR_TERRAIN_QUERY": "TestContractHTTP",
	"AR_TERRAIN_SIZE":  "TestContractConstants (derived)",
	"AR_TRACK_PATH":    "TestContractHTTP",
	"AR_TRACK_QUERY":   "TestContractHTTP",
	"AR_STRIP_PATH":    "TestContractHTTP",
	"AR_STRIP_QUERY":   "TestContractHTTP",
	"AR_MDNS_SERVICE":  "TestContractDiscovery",
	"AR_MDNS_QUERY_ID": "TestContractDiscovery",
	"AR_MDNS_SRC_PORT": "TestContractDiscovery",
}

func TestContractConstants(t *testing.T) {
	h := loadHeader(t)
	var names []string
	for n := range h.ints {
		names = append(names, n)
	}
	for n := range h.strs {
		names = append(names, n)
	}
	for n := range h.exprs {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		if want, ok := relayInts[n]; ok {
			if got := h.ints[n]; got != want {
				t.Errorf("%s: arframe.h says %d (%#x), relay has %d (%#x)", n, got, got, want, want)
			}
		} else if _, ok := checkedElsewhere[n]; !ok {
			t.Errorf("%s is new in arframe.h: map it to a relay value in contract_test.go", n)
		}
	}
	for n := range relayInts {
		if _, ok := h.ints[n]; !ok {
			t.Errorf("%s: relay maps it but arframe.h no longer defines it", n)
		}
	}
	if got := h.ints["AR_HDR_SIZE"] + h.ints["AR_MAX_RECS"]*h.ints["AR_REC_SIZE_V1"]; h.exprs["AR_FRAME_MAX"] == "" ||
		got != hdrSize+maxRecs*recSize {
		t.Errorf("AR_FRAME_MAX: want hdr + max*rec = %d", hdrSize+maxRecs*recSize)
	}
	if h.exprs["AR_TERRAIN_SIZE"] != "(AR_TERRAIN_HDR + 2 * AR_TN * AR_TN)" || terrainSize != terrainHdr+2*tn*tn {
		t.Errorf("AR_TERRAIN_SIZE: want (AR_TERRAIN_HDR + 2 * AR_TN * AR_TN) = %d, got %q", terrainSize,
			h.exprs["AR_TERRAIN_SIZE"])
	}
}

func be(v uint64, size int) []byte {
	b := make([]byte, size)
	for i := size - 1; i >= 0; i-- {
		b[i] = byte(v)
		v >>= 8
	}
	return b
}

func TestContractLayout(t *testing.T) {
	h := loadHeader(t)
	r := record{icao: 0x0A0B0C, flags: 0x7F, x: 0x1122, y: -0x3344, alt: 0x2D2E, squawk: 0x7700,
		gs: 0xC3, track: 0x3C, vrate: -5, cat: 0x11, cs: [8]byte{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}}
	f := frame{seq: 0x1234, time: 0x89ABCDEF, rangeNM: 0x0102, source: 0x5A, hflags: 0xA5, recs: []record{r}}
	enc := f.encode()
	d := detail{flags: 0x7F, icao: 0x0D0E0F, cat: 0x15, geomAlt: -0x1357, selAlt: 0x2468, selHdg: 271,
		qnh: 10264, year: 2017, seen: 0x0A0B}
	padASCII(d.reg[:], "N8713M")
	padASCII(d.typ[:], "B38M")
	padASCII(d.desc[:], "BOEING 737 MAX 8")
	padASCII(d.owner[:], "SOUTHWEST AIRLINES CO")
	encD := d.encode()
	var p patch
	p.g = geom{lat: 36.12345, lon: -86.54321, cellM: 0x0123, levelCm: 0x0456, baseM: -0x0789}
	p.flags, p.hmax = 0x81, 0x3F
	p.height[0], p.colour[0] = 0x2A, 0x0B
	encT := p.encode()
	tk := trackRec{flags: 0x0F, x: 0x01020304, y: -0x05060708, z: 0x090A0B0C, vx: -0x1112, vy: 0x1314, vz: -0x1516,
		heading: 0xC001, bank: -27, age: 0xC8, alt: -0x1718, gs: 0x191A, vrate: -0x1B1C,
		cs: [8]byte{'S', 'W', 'A', '6', '9', '2', ' ', ' '}}
	encK := tk.encode()

	// What the cartridge must read at each C field (big-endian, the field's C size).
	want := map[string]map[string]func(size int) []byte{
		"ar_hdr": {
			"magic":    func(int) []byte { return []byte("AR") },
			"ver":      func(n int) []byte { return be(frameVer, n) },
			"count":    func(n int) []byte { return be(1, n) },
			"rec_size": func(n int) []byte { return be(recSize, n) },
			"seq":      func(n int) []byte { return be(uint64(f.seq), n) },
			"time":     func(n int) []byte { return be(uint64(f.time), n) },
			"range_nm": func(n int) []byte { return be(uint64(f.rangeNM), n) },
			"source":   func(n int) []byte { return be(uint64(f.source), n) },
			"hflags":   func(n int) []byte { return be(uint64(f.hflags), n) },
		},
		"ar_rec": {
			"icao":   func(n int) []byte { return be(uint64(r.icao), n) },
			"flags":  func(n int) []byte { return be(uint64(r.flags), n) },
			"x":      func(n int) []byte { return be(uint64(uint16(r.x)), n) },
			"y":      func(n int) []byte { return be(uint64(uint16(r.y)), n) },
			"alt":    func(n int) []byte { return be(uint64(uint16(r.alt)), n) },
			"squawk": func(n int) []byte { return be(uint64(r.squawk), n) },
			"gs":     func(n int) []byte { return be(uint64(r.gs), n) },
			"track":  func(n int) []byte { return be(uint64(r.track), n) },
			"vrate":  func(n int) []byte { return be(uint64(uint8(r.vrate)), n) },
			"cat":    func(n int) []byte { return be(uint64(r.cat), n) },
			"cs":     func(int) []byte { return r.cs[:] },
		},
		"ar_detail": {
			"magic":    func(int) []byte { return []byte("AD") },
			"ver":      func(n int) []byte { return be(frameVer, n) },
			"dflags":   func(n int) []byte { return be(uint64(d.flags), n) },
			"icao":     func(n int) []byte { return be(uint64(d.icao), n) },
			"cat":      func(n int) []byte { return be(uint64(d.cat), n) },
			"geom_alt": func(n int) []byte { return be(uint64(uint16(d.geomAlt)), n) },
			"sel_alt":  func(n int) []byte { return be(uint64(uint16(d.selAlt)), n) },
			"sel_hdg":  func(n int) []byte { return be(uint64(d.selHdg), n) },
			"qnh":      func(n int) []byte { return be(uint64(d.qnh), n) },
			"year":     func(n int) []byte { return be(uint64(d.year), n) },
			"reg":      func(int) []byte { return d.reg[:] },
			"type":     func(int) []byte { return d.typ[:] },
			"seen":     func(n int) []byte { return be(uint64(d.seen), n) },
			"desc":     func(int) []byte { return d.desc[:] },
			"owner":    func(int) []byte { return d.owner[:] },
		},
		"ar_terrain": {
			"magic":    func(int) []byte { return []byte("AT") },
			"ver":      func(n int) []byte { return be(frameVer, n) },
			"tflags":   func(n int) []byte { return be(uint64(p.flags), n) },
			"n":        func(n int) []byte { return be(tn, n) },
			"cell_m":   func(n int) []byte { return be(uint64(p.g.cellM), n) },
			"level_cm": func(n int) []byte { return be(uint64(p.g.levelCm), n) },
			"base_m":   func(n int) []byte { return be(uint64(uint16(int16(p.g.baseM))), n) },
			"hmax":     func(n int) []byte { return be(uint64(p.hmax), n) },
			"rsv":      func(n int) []byte { return be(0, n) },
			"pal": func(n int) []byte {
				var b []byte
				for _, c := range terrainPal {
					b = append(b, be(uint64(c), 2)...)
				}
				return b
			},
			"key": func(n int) []byte {
				b := make([]byte, n)
				copy(b, p.g.key())
				return b
			},
			"pad": func(n int) []byte { return be(0, n) },
		},
		"ar_track": {
			"magic":   func(int) []byte { return []byte("AK") },
			"ver":     func(n int) []byte { return be(frameVer, n) },
			"kflags":  func(n int) []byte { return be(uint64(tk.flags), n) },
			"x":       func(n int) []byte { return be(uint64(uint32(tk.x)), n) },
			"y":       func(n int) []byte { return be(uint64(uint32(tk.y)), n) },
			"z":       func(n int) []byte { return be(uint64(uint32(tk.z)), n) },
			"vx":      func(n int) []byte { return be(uint64(uint16(tk.vx)), n) },
			"vy":      func(n int) []byte { return be(uint64(uint16(tk.vy)), n) },
			"vz":      func(n int) []byte { return be(uint64(uint16(tk.vz)), n) },
			"heading": func(n int) []byte { return be(uint64(tk.heading), n) },
			"bank":    func(n int) []byte { return be(uint64(uint8(tk.bank)), n) },
			"age":     func(n int) []byte { return be(uint64(tk.age), n) },
			"alt":     func(n int) []byte { return be(uint64(uint16(tk.alt)), n) },
			"gs":      func(n int) []byte { return be(uint64(tk.gs), n) },
			"vrate":   func(n int) []byte { return be(uint64(uint16(tk.vrate)), n) },
			"cs":      func(int) []byte { return tk.cs[:] },
		},
	}
	encs := map[string][]byte{"ar_hdr": enc, "ar_rec": enc, "ar_detail": encD, "ar_terrain": encT, "ar_track": encK}
	base := map[string]int{"ar_hdr": 0, "ar_rec": hdrSize, "ar_detail": 0, "ar_terrain": 0, "ar_track": 0}
	size := map[string]int{"ar_hdr": int(h.ints["AR_HDR_SIZE"]), "ar_rec": int(h.ints["AR_REC_SIZE_V1"]),
		"ar_detail": int(h.ints["AR_DETAIL_SIZE"]), "ar_terrain": int(h.ints["AR_TERRAIN_HDR"]),
		"ar_track": int(h.ints["AR_TRACK_SIZE"])}
	// The maps follow the terrain header: heights, then colours.
	if len(encT) != terrainSize || encT[terrainHdr] != p.height[0] || encT[terrainHdr+tn*tn] != p.colour[0] {
		t.Errorf("terrain body: %d bytes, height[0] %#x, colour[0] %#x", len(encT), encT[terrainHdr], encT[terrainHdr+tn*tn])
	}

	var structs []string
	for st := range h.order {
		structs = append(structs, st)
		if _, ok := want[st]; !ok {
			t.Errorf("struct %s is new in arframe.h: encode it in the relay and map it in contract_test.go", st)
		}
	}
	sort.Strings(structs)
	for st := range want {
		if len(h.order[st]) == 0 {
			t.Errorf("struct %s: relay encodes it but arframe.h no longer has it", st)
		}
	}
	for _, st := range structs {
		enc := encs[st]
		if enc == nil {
			continue
		}
		end := 0
		for _, field := range h.order[st] {
			off, ok := h.offsets[st][field]
			if !ok {
				t.Errorf("%s.%s has no AR_AT offset assert in arframe.h", st, field)
				continue
			}
			n := h.fields[st][field]
			if off != end {
				t.Errorf("%s.%s at %d: gap or overlap (previous field ends at %d)", st, field, off, end)
			}
			end = off + n
			exp, ok := want[st][field]
			if !ok {
				t.Errorf("%s.%s is new in arframe.h: encode it in the relay and map it in contract_test.go", st, field)
				continue
			}
			if got := enc[base[st]+off : base[st]+off+n]; !bytes.Equal(got, exp(n)) {
				t.Errorf("%s.%s @%d: relay wrote % x, cartridge expects % x", st, field, off, got, exp(n))
			}
		}
		if end != size[st] {
			t.Errorf("%s: fields end at %d, size is %d", st, end, size[st])
		}
		for field := range want[st] {
			if _, ok := h.fields[st][field]; !ok {
				t.Errorf("%s.%s: relay encodes it but arframe.h no longer has it", st, field)
			}
		}
	}
}

// cFormat turns a C printf format into a Go one (%u -> %d; %s and %02x are the same).
func cFormat(f string) string { return strings.ReplaceAll(f, "%u", "%d") }

func TestContractHTTP(t *testing.T) {
	h := loadHeader(t)
	ver := h.ints["AR_VER"]
	if h.strs["AR_FRAME_PATH"] != framePath {
		t.Errorf("AR_FRAME_PATH %q, relay serves %q", h.strs["AR_FRAME_PATH"], framePath)
	}
	if h.strs["AR_DETAIL_PATH"] != detailPath {
		t.Errorf("AR_DETAIL_PATH %q, relay serves %q", h.strs["AR_DETAIL_PATH"], detailPath)
	}
	// Build the queries exactly as the cart does (sprintf with the AR_*_QUERY formats), then serve them.
	format := h.strs["AR_FRAME_QUERY"]
	if strings.Count(format, "%") != 5 {
		t.Fatalf("AR_FRAME_QUERY %q: expected lat, lon, range, max, version", format)
	}
	q := fmt.Sprintf(cFormat(format), "36.1245", "-86.6782", 50, 40, ver)
	req := httptest.NewRequest("GET", framePath+q, nil)
	got, err := parseQuery(req)
	if err != nil {
		t.Fatalf("relay rejects the cart's query %q: %v", q, err)
	}
	if got.lat != 36.1245 || got.lon != -86.6782 || got.rangeNM != 50 || got.max != 40 {
		t.Errorf("relay read %q as %+v", q, got)
	}
	if !versionOK(httptest.NewRecorder(), req) {
		t.Errorf("relay refuses the cart's version in %q", q)
	}
	old := httptest.NewRecorder()
	if versionOK(old, httptest.NewRequest("GET", framePath+fmt.Sprintf(cFormat(format), "1", "1", 5, 5, ver+1), nil)) ||
		old.Code != int(h.ints["AR_HTTP_MISMATCH"]) {
		t.Errorf("a cart at another version gets %d, want AR_HTTP_MISMATCH", old.Code)
	}

	dq := h.strs["AR_DETAIL_QUERY"]
	if strings.Count(dq, "%") != 4 {
		t.Fatalf("AR_DETAIL_QUERY %q: expected 3 icao bytes, version", dq)
	}
	src := newSource("http://upstream.invalid", time.Second)
	src.last, src.lastAt = &upstreamResp{AC: []upstreamAC{{Hex: "abfcda", Reg: "N8713M"}}}, time.Now()
	rec := httptest.NewRecorder()
	detailHandler(src).ServeHTTP(rec, httptest.NewRequest("GET", detailPath+fmt.Sprintf(cFormat(dq), 0xab, 0xfc, 0xda, ver), nil))
	body := rec.Body.Bytes()
	if rec.Code != 200 || len(body) != int(h.ints["AR_DETAIL_SIZE"]) || body[3]&byte(h.ints["AR_D_FOUND"]) == 0 {
		t.Errorf("detail for the cart's query: status %d, %d bytes % x", rec.Code, len(body), body)
	}

	// Cockpit: the terrain patch for an aircraft, then its track placed in that patch by the patch's key.
	for _, k := range []string{"AR_TERRAIN_PATH", "AR_TRACK_PATH"} {
		if want := map[string]string{"AR_TERRAIN_PATH": terrainPath, "AR_TRACK_PATH": trackPath}[k]; h.strs[k] != want {
			t.Errorf("%s %q, relay serves %q", k, h.strs[k], want)
		}
	}
	lat, lon, alt, gs, trk := 36.2, -86.7, 9000.0, 300.0, 90.0
	src.last.AC[0].Lat, src.last.AC[0].Lon, src.last.AC[0].AltBaro = &lat, &lon, alt
	src.last.AC[0].GS, src.last.AC[0].Track = &gs, &trk
	el := flatDEM(150)
	tq := h.strs["AR_TERRAIN_QUERY"]
	if strings.Count(tq, "%") != 4 {
		t.Fatalf("AR_TERRAIN_QUERY %q: expected 3 icao bytes, version", tq)
	}
	rec = httptest.NewRecorder()
	terrainHandler(src, el).ServeHTTP(rec, httptest.NewRequest("GET", terrainPath+fmt.Sprintf(cFormat(tq), 0xab, 0xfc, 0xda, ver), nil))
	body = rec.Body.Bytes()
	if rec.Code != 200 || len(body) != terrainSize || string(body[:2]) != "AT" {
		t.Fatalf("terrain for the cart's query: status %d, %d bytes", rec.Code, len(body))
	}
	key := string(bytes.TrimRight(body[46:46+keyLen], "\x00"))
	kq := h.strs["AR_TRACK_QUERY"]
	if strings.Count(kq, "%") != 5 {
		t.Fatalf("AR_TRACK_QUERY %q: expected 3 icao bytes, key, version", kq)
	}
	rec = httptest.NewRecorder()
	trackHandler(src, el, newTurns()).ServeHTTP(rec, httptest.NewRequest("GET", trackPath+fmt.Sprintf(cFormat(kq), 0xab, 0xfc, 0xda, key, ver), nil))
	body = rec.Body.Bytes()
	if rec.Code != 200 || len(body) != trackSize || body[3]&kFound == 0 {
		t.Fatalf("track for the cart's query (key %q): status %d, % x", key, rec.Code, body)
	}
	// A strip of the patch's grid, as the cart asks for one (negative x: west of the first patch).
	if h.strs["AR_STRIP_PATH"] != stripPath {
		t.Errorf("AR_STRIP_PATH %q, relay serves %q", h.strs["AR_STRIP_PATH"], stripPath)
	}
	sq := h.strs["AR_STRIP_QUERY"]
	if strings.Count(sq, "%") != 6 {
		t.Fatalf("AR_STRIP_QUERY %q: expected key, x, y, w, h, version", sq)
	}
	rec = httptest.NewRecorder()
	stripHandler(el).ServeHTTP(rec, httptest.NewRequest("GET", stripPath+fmt.Sprintf(cFormat(sq), key, -8, 0, 8, tn, ver), nil))
	if rec.Code != 200 || rec.Body.Len() != 2*8*tn || 8*tn > int(h.ints["AR_STRIP_CELLS"]) {
		t.Errorf("strip for the cart's query: status %d, %d bytes", rec.Code, rec.Body.Len())
	}

	// The patch is centred on the aircraft (projected terrainLeadS ahead): it sits near the middle cell.
	x, y := int32(binary.BigEndian.Uint32(body[4:])), int32(binary.BigEndian.Uint32(body[8:]))
	if x < 62*256 || x > 66*256 || y < 62*256 || y > 66*256 {
		t.Errorf("track at cell %.2f,%.2f, want the patch centre (64, 64)", float64(x)/256, float64(y)/256)
	}
}

// contract.lock: "AR_VER <n> <sha256 of arframe.h without comments and whitespace>".
const lockPath = "contract.lock"

func headerHash(t *testing.T) string {
	src, err := os.ReadFile(headerPath)
	if err != nil {
		t.Skip("no header")
	}
	s := regexp.MustCompile(`(?s)/\*.*?\*/`).ReplaceAllString(string(src), "")
	s = strings.Join(strings.Fields(s), " ")
	return fmt.Sprintf("%x", sha256.Sum256([]byte(s)))
}

// TestContractLock: arframe.h can't change without an AR_VER bump. After bumping it, record the
// new hash with `make contract-lock` (CONTRACT_LOCK=update).
func TestContractLock(t *testing.T) {
	h := loadHeader(t)
	ver, sum := h.ints["AR_VER"], headerHash(t)
	line := fmt.Sprintf("AR_VER %d %s\n", ver, sum)
	if os.Getenv("CONTRACT_LOCK") == "update" {
		if err := os.WriteFile(lockPath, []byte(line), 0o644); err != nil {
			t.Fatal(err)
		}
		return
	}
	data, err := os.ReadFile(lockPath)
	if err != nil {
		t.Fatalf("%s missing: run make contract-lock", lockPath)
	}
	var lockVer int64
	var lockSum string
	fmt.Sscanf(string(data), "AR_VER %d %s", &lockVer, &lockSum)
	switch {
	case lockSum == sum && lockVer == ver:
	case lockVer == ver:
		t.Errorf("arframe.h changed but AR_VER is still %d: bump AR_VER (relay and cart must change together), then make contract-lock", ver)
	default:
		t.Errorf("AR_VER is now %d (lock has %d): run make contract-lock to record the new contract", ver, lockVer)
	}
}

func TestContractDiscovery(t *testing.T) {
	h := loadHeader(t)
	if h.strs["AR_MDNS_SERVICE"]+"." != serviceName {
		t.Errorf("AR_MDNS_SERVICE %q, relay advertises %q", h.strs["AR_MDNS_SERVICE"], serviceName)
	}
	if h.ints["AR_MDNS_SRC_PORT"] == mdnsPort {
		t.Errorf("AR_MDNS_SRC_PORT must not be %d, or the query isn't legacy unicast", mdnsPort)
	}
	// The query as src/net.c build_query() makes it: header, AR_MDNS_SERVICE labels, PTR, IN.
	id := uint16(h.ints["AR_MDNS_QUERY_ID"])
	qb := []byte{byte(id >> 8), byte(id), 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}
	qb = putName(qb, h.strs["AR_MDNS_SERVICE"]+".")
	qb = append(qb, 0, typePTR, 0, classIN)
	qid, qs, err := parseDNSQuery(qb)
	if err != nil {
		t.Fatalf("relay can't parse the cart's query: %v", err)
	}
	resp := newAdvert("relay-host", 8080).answer(qid, qs, net.IPv4(192, 168, 1, 20), true)
	if resp == nil {
		t.Fatal("relay doesn't answer the cart's query")
	}
	if binary.BigEndian.Uint16(resp) != id {
		t.Errorf("reply id %#x, cart expects %#x", binary.BigEndian.Uint16(resp), id)
	}
	for _, txt := range []string{fmt.Sprintf("v=%d", h.ints["AR_VER"]), "path=" + h.strs["AR_FRAME_PATH"]} {
		if !bytes.Contains(resp, []byte(txt)) {
			t.Errorf("mDNS TXT is missing %q", txt)
		}
	}
}
