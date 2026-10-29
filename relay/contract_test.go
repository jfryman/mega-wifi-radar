package main

// Contract test: src/arframe.h is the single source of truth for what the relay and the cartridge
// agree on (lode/architecture/contract.md). This test parses it and checks the relay against every
// AR_* item: constants, the frame layout (each field at its AR_AT offset), the HTTP path and query,
// and the mDNS discovery strings. Any AR_* item the test doesn't know about fails it, so a new
// contract item can't slip past.

import (
	"bytes"
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
}

// Items checked by their own tests below, or reserved values the relay never emits.
var checkedElsewhere = map[string]string{
	"AR_SRC_ADSBLOL":   "reserved source id (relay only emits adsb.fi)",
	"AR_SRC_OPENSKY":   "reserved source id (relay only emits adsb.fi)",
	"AR_FRAME_MAX":     "TestContractConstants (derived)",
	"AR_FRAME_PATH":    "TestContractHTTP",
	"AR_FRAME_QUERY":   "TestContractHTTP",
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
	}
	base := map[string]int{"ar_hdr": 0, "ar_rec": hdrSize}
	size := map[string]int{"ar_hdr": int(h.ints["AR_HDR_SIZE"]), "ar_rec": int(h.ints["AR_REC_SIZE_V1"])}

	for _, st := range []string{"ar_hdr", "ar_rec"} {
		if len(h.order[st]) == 0 {
			t.Fatalf("struct %s not found in arframe.h", st)
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

func TestContractHTTP(t *testing.T) {
	h := loadHeader(t)
	if h.strs["AR_FRAME_PATH"] != framePath {
		t.Errorf("AR_FRAME_PATH %q, relay serves %q", h.strs["AR_FRAME_PATH"], framePath)
	}
	// Build the query exactly as the cart does (sprintf with AR_FRAME_QUERY), then parse it.
	format := h.strs["AR_FRAME_QUERY"]
	if strings.Count(format, "%") != 4 {
		t.Fatalf("AR_FRAME_QUERY %q: expected lat, lon, range, max", format)
	}
	q := fmt.Sprintf(strings.ReplaceAll(format, "%u", "%d"), "36.1245", "-86.6782", 50, 40)
	got, err := parseQuery(httptest.NewRequest("GET", framePath+q, nil))
	if err != nil {
		t.Fatalf("relay rejects the cart's query %q: %v", q, err)
	}
	if got.lat != 36.1245 || got.lon != -86.6782 || got.rangeNM != 50 || got.max != 40 {
		t.Errorf("relay read %q as %+v", q, got)
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
