package main

import (
	"encoding/binary"
	"net"
	"testing"
)

// The query the cartridge sends (src/discover.c): id 0x4D57, one PTR question, QM.
func cartQuery() []byte {
	b := []byte{0x4D, 0x57, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}
	b = putName(b, serviceName)
	b = binary.BigEndian.AppendUint16(b, typePTR)
	return binary.BigEndian.AppendUint16(b, classIN)
}

type rr struct {
	name  string
	rtype uint16
	ttl   uint32
	data  []byte
	off   int // rdata offset in msg (for names inside rdata)
}

func parseResponse(t *testing.T, msg []byte) (uint16, int, []rr) {
	t.Helper()
	id := binary.BigEndian.Uint16(msg)
	nq := int(binary.BigEndian.Uint16(msg[4:]))
	n := int(binary.BigEndian.Uint16(msg[6:])) + int(binary.BigEndian.Uint16(msg[8:])) + int(binary.BigEndian.Uint16(msg[10:]))
	off := 12
	for i := 0; i < nq; i++ {
		_, next, err := readName(msg, off)
		if err != nil {
			t.Fatal(err)
		}
		off = next + 4
	}
	var out []rr
	for i := 0; i < n; i++ {
		name, next, err := readName(msg, off)
		if err != nil {
			t.Fatal(err)
		}
		rt := binary.BigEndian.Uint16(msg[next:])
		ttl := binary.BigEndian.Uint32(msg[next+4:])
		l := int(binary.BigEndian.Uint16(msg[next+8:]))
		out = append(out, rr{name, rt, ttl, msg[next+10 : next+10+l], next + 10})
		off = next + 10 + l
	}
	return id, nq, out
}

func TestLegacyUnicastAnswer(t *testing.T) {
	a := newAdvert("relay-host.example.lan", 8080)
	id, qs, err := parseDNSQuery(cartQuery())
	if err != nil || len(qs) != 1 || qs[0].name != serviceName || qs[0].qtype != typePTR {
		t.Fatalf("parse: %v %+v", err, qs)
	}
	resp := a.answer(id, qs, net.IPv4(192, 168, 1, 20), true)
	if resp == nil {
		t.Fatal("no answer")
	}
	rid, nq, rrs := parseResponse(t, resp)
	if rid != 0x4D57 || nq != 1 {
		t.Errorf("legacy reply must echo id and question: id %#x, questions %d", rid, nq)
	}
	var port uint16
	var ip net.IP
	for _, r := range rrs {
		if r.ttl > legacyTTL {
			t.Errorf("%s TTL %d > %d", r.name, r.ttl, legacyTTL)
		}
		switch r.rtype {
		case typeSRV:
			port = binary.BigEndian.Uint16(r.data[4:])
			if target, _, _ := readName(resp, r.off+6); target != "relay-host.local." {
				t.Errorf("SRV target %q", target)
			}
		case typeA:
			ip = net.IP(r.data)
		}
	}
	if port != 8080 || !ip.Equal(net.IPv4(192, 168, 1, 20)) {
		t.Errorf("got %v:%d, want 192.168.1.20:8080", ip, port)
	}
}

func TestMulticastAnswerAndOtherQueries(t *testing.T) {
	a := newAdvert("relay-host", 8080)
	_, qs, _ := parseDNSQuery(cartQuery())
	resp := a.answer(0x1234, qs, net.IPv4(10, 0, 0, 5), false)
	if id, nq, rrs := parseResponse(t, resp); id != 0 || nq != 0 || rrs[0].ttl != normalTTL {
		t.Errorf("normal mDNS reply: id %#x, questions %d, ttl %d", id, nq, rrs[0].ttl)
	}
	other := []question{{name: "_http._tcp.local.", qtype: typePTR}}
	if a.answer(1, other, net.IPv4(10, 0, 0, 5), true) != nil {
		t.Error("answered a query for another service")
	}
}

func TestReadNameCompression(t *testing.T) {
	msg := putName(make([]byte, 12), "relay-host.local.")
	msg = append(msg, 0xC0, 12) // pointer back to offset 12
	name, next, err := readName(msg, len(msg)-2)
	if err != nil || name != "relay-host.local." || next != len(msg) {
		t.Errorf("got %q %d %v", name, next, err)
	}
}
