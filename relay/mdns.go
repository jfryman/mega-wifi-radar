package main

// Minimal mDNS / DNS-SD responder (stdlib only) advertising the relay as _mwradar._tcp
// (lode/net/discovery.md). It answers:
//   - legacy unicast queries (source port != 5353), which is what the cartridge sends: unicast
//     reply to the querier, query ID and question echoed, TTL <= 10 s (RFC 6762 6.7);
//   - normal mDNS queries (from 5353), with a multicast reply, so dns-sd -B / avahi-browse see us;
//   - queries with the QU bit set, with a unicast reply.

import (
	"encoding/binary"
	"errors"
	"fmt"
	"log"
	"net"
	"strings"
)

const (
	mdnsPort    = 5353
	serviceName = "_mwradar._tcp.local."
	typeA       = 1
	typePTR     = 12
	typeTXT     = 16
	typeSRV     = 33
	typeANY     = 255
	classIN     = 1
	cacheFlush  = 0x8000
	legacyTTL   = 10
	normalTTL   = 120
)

var mdnsGroup = &net.UDPAddr{IP: net.IPv4(224, 0, 0, 251), Port: mdnsPort}

type advert struct {
	instance string // "MegaWiFi Radar._mwradar._tcp.local."
	host     string // "relay-host.local."
	port     uint16
	txt      []string
}

func newAdvert(hostname string, port uint16) advert {
	h := strings.TrimSuffix(strings.Split(hostname, ".")[0], ".local")
	return advert{
		instance: "MegaWiFi Radar." + serviceName,
		host:     h + ".local.",
		port:     port,
		txt:      []string{fmt.Sprintf("v=%d", frameVer), "path=" + framePath},
	}
}

type question struct {
	name  string
	qtype uint16
	qu    bool // unicast response requested
}

// readName reads a possibly compressed DNS name at off, returning it (dot-terminated) and the
// offset just past it in the original position.
func readName(msg []byte, off int) (string, int, error) {
	var labels []string
	end, jumps := -1, 0
	for {
		if off >= len(msg) {
			return "", 0, errors.New("name past end")
		}
		l := int(msg[off])
		switch {
		case l == 0:
			if end < 0 {
				end = off + 1
			}
			return strings.Join(labels, ".") + ".", end, nil
		case l&0xC0 == 0xC0:
			if off+1 >= len(msg) || jumps > 16 {
				return "", 0, errors.New("bad pointer")
			}
			if end < 0 {
				end = off + 2
			}
			off = int(binary.BigEndian.Uint16(msg[off:]) & 0x3FFF)
			jumps++
		default:
			if off+1+l > len(msg) {
				return "", 0, errors.New("label past end")
			}
			labels = append(labels, string(msg[off+1:off+1+l]))
			off += 1 + l
		}
	}
}

func parseDNSQuery(msg []byte) (id uint16, qs []question, err error) {
	if len(msg) < 12 {
		return 0, nil, errors.New("short")
	}
	id = binary.BigEndian.Uint16(msg)
	if msg[2]&0x80 != 0 { // a response, not a query
		return 0, nil, errors.New("not a query")
	}
	n := int(binary.BigEndian.Uint16(msg[4:]))
	off := 12
	for i := 0; i < n; i++ {
		name, next, err := readName(msg, off)
		if err != nil || next+4 > len(msg) {
			return 0, nil, errors.New("bad question")
		}
		qt := binary.BigEndian.Uint16(msg[next:])
		qc := binary.BigEndian.Uint16(msg[next+2:])
		qs = append(qs, question{strings.ToLower(name), qt, qc&0x8000 != 0})
		off = next + 4
	}
	return id, qs, nil
}

func putName(b []byte, name string) []byte {
	for _, l := range strings.Split(strings.TrimSuffix(name, "."), ".") {
		b = append(b, byte(len(l)))
		b = append(b, l...)
	}
	return append(b, 0)
}

func putRR(b []byte, name string, rtype uint16, class uint16, ttl uint32, rdata []byte) []byte {
	b = putName(b, name)
	b = binary.BigEndian.AppendUint16(b, rtype)
	b = binary.BigEndian.AppendUint16(b, class)
	b = binary.BigEndian.AppendUint32(b, ttl)
	b = binary.BigEndian.AppendUint16(b, uint16(len(rdata)))
	return append(b, rdata...)
}

// answer builds the reply for qs, or nil if none of them is about us. legacy: echo id and
// questions, short TTLs, no cache-flush bits (RFC 6762 6.7).
func (a advert) answer(id uint16, qs []question, ip net.IP, legacy bool) []byte {
	ours := false
	for _, q := range qs {
		switch {
		case q.name == serviceName && (q.qtype == typePTR || q.qtype == typeANY),
			q.name == strings.ToLower(a.instance), q.name == strings.ToLower(a.host):
			ours = true
		}
	}
	if !ours || ip.To4() == nil {
		return nil
	}
	ttl, flush := uint32(normalTTL), uint16(cacheFlush)
	if legacy {
		ttl, flush = legacyTTL, 0
	}

	var srv []byte
	srv = binary.BigEndian.AppendUint16(srv, 0) // priority
	srv = binary.BigEndian.AppendUint16(srv, 0) // weight
	srv = binary.BigEndian.AppendUint16(srv, a.port)
	srv = putName(srv, a.host)
	var txt []byte
	for _, t := range a.txt {
		txt = append(txt, byte(len(t)))
		txt = append(txt, t...)
	}

	b := make([]byte, 12)
	if legacy {
		binary.BigEndian.PutUint16(b, id)
	}
	b[2] = 0x84 // response, authoritative
	nq := 0
	if legacy {
		nq = len(qs)
		binary.BigEndian.PutUint16(b[4:], uint16(nq))
		for _, q := range qs {
			b = putName(b, q.name)
			b = binary.BigEndian.AppendUint16(b, q.qtype)
			b = binary.BigEndian.AppendUint16(b, classIN)
		}
	}
	b = putRR(b, serviceName, typePTR, classIN, ttl, putName(nil, a.instance))
	b = putRR(b, a.instance, typeSRV, classIN|flush, ttl, srv)
	b = putRR(b, a.instance, typeTXT, classIN|flush, ttl, txt)
	b = putRR(b, a.host, typeA, classIN|flush, ttl, ip.To4())
	binary.BigEndian.PutUint16(b[6:], 1)  // answers: PTR
	binary.BigEndian.PutUint16(b[10:], 3) // additional: SRV, TXT, A
	return b
}

// localIPFor picks this host's IPv4 on the subnet that contains peer (else the first non-loopback).
func localIPFor(peer net.IP) net.IP {
	var first net.IP
	ifs, _ := net.Interfaces()
	for _, ifc := range ifs {
		if ifc.Flags&net.FlagUp == 0 || ifc.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, _ := ifc.Addrs()
		for _, ad := range addrs {
			n, ok := ad.(*net.IPNet)
			if !ok || n.IP.To4() == nil {
				continue
			}
			if n.Contains(peer) {
				return n.IP.To4()
			}
			if first == nil {
				first = n.IP.To4()
			}
		}
	}
	return first
}

// serveMDNS answers queries until the socket fails. Errors are logged; the HTTP relay keeps running.
func serveMDNS(a advert) {
	conn, err := net.ListenMulticastUDP("udp4", nil, mdnsGroup)
	if err != nil {
		log.Printf("mdns: disabled: %v", err)
		return
	}
	log.Printf("mdns: advertising %s on port %d (host %s)", a.instance, a.port, a.host)
	buf := make([]byte, 9000)
	for {
		n, from, err := conn.ReadFromUDP(buf)
		if err != nil {
			log.Printf("mdns: %v", err)
			return
		}
		id, qs, err := parseDNSQuery(buf[:n])
		if err != nil {
			continue
		}
		legacy := from.Port != mdnsPort
		unicast := legacy
		for _, q := range qs {
			unicast = unicast || q.qu
		}
		resp := a.answer(id, qs, localIPFor(from.IP), legacy)
		if resp == nil {
			continue
		}
		dst := mdnsGroup
		if unicast {
			dst = from
		}
		if _, err := conn.WriteToUDP(resp, dst); err != nil {
			log.Printf("mdns: reply to %v: %v", dst, err)
			continue
		}
		log.Printf("mdns: answered %v (legacy %v) -> %v", from, legacy, dst)
	}
}
