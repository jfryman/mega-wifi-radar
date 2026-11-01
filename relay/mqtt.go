package main

// A minimal MQTT 3.1.1 subscriber, stdlib only (like mdns.go): CONNECT with a clean session, SUBSCRIBE at
// QoS 1, PUBLISH in (QoS 0/1, acknowledged), PINGREQ, and an empty retained PUBLISH to clear a retained topic.
// Enough to read the owner's atc-mqtt flight feed (lode/plans/flyover-notices.md); no TLS, no QoS 2 out.

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"strconv"
	"sync"
	"time"
)

const (
	mqttConnect    = 0x10
	mqttConnack    = 0x20
	mqttPublish    = 0x30
	mqttPuback     = 0x40
	mqttSubscribe  = 0x82 // SUBSCRIBE has the reserved flags 0010
	mqttSuback     = 0x90
	mqttPingreq    = 0xC0
	mqttPingresp   = 0xD0
	mqttDisconnect = 0xE0
)

type mqttConfig struct {
	host, user, pass string
	port             int
	topic, clientID  string
	clear            bool // after a retained message, publish an empty retained one to clear the topic
	keepalive        time.Duration
}

func (c mqttConfig) addr() string { return net.JoinHostPort(c.host, strconv.Itoa(c.port)) }

// mqttMessage is one PUBLISH received on the subscription.
type mqttMessage struct {
	topic    string
	payload  []byte
	retained bool
}

// mqttClientID is unique per process: the broker drops an older session with the same id, and with a clean
// session there's nothing to resume, so two relays (or a restarted pod) must never share one.
func mqttClientID() string {
	b := make([]byte, 4)
	rand.Read(b)
	return "mwradar-relay-" + hex.EncodeToString(b)
}

// runMQTT stays subscribed until ctx ends, reconnecting with back-off (5, 10, 30, 60 s; reset by a CONNACK).
func runMQTT(ctx context.Context, cfg mqttConfig, handle func(mqttMessage)) {
	backoff := []time.Duration{5 * time.Second, 10 * time.Second, 30 * time.Second, 60 * time.Second}
	fails := 0
	for ctx.Err() == nil {
		connected, err := mqttSession(ctx, cfg, handle)
		if ctx.Err() != nil {
			return
		}
		if connected {
			fails = 0
		}
		wait := backoff[min(fails, len(backoff)-1)]
		fails++
		log.Printf("mqtt: %s: %v; retrying in %v", cfg.addr(), err, wait)
		select {
		case <-ctx.Done():
			return
		case <-time.After(wait):
		}
	}
}

// mqttSession runs one connection. connected reports whether the broker accepted it.
func mqttSession(ctx context.Context, cfg mqttConfig, handle func(mqttMessage)) (connected bool, err error) {
	d := net.Dialer{Timeout: 10 * time.Second}
	conn, err := d.DialContext(ctx, "tcp", cfg.addr())
	if err != nil {
		return false, err
	}
	defer conn.Close()
	stop := context.AfterFunc(ctx, func() { conn.Close() })
	defer stop()
	return mqttServe(conn, cfg, handle)
}

// mqttServe speaks the protocol over an open connection (split out so tests can use net.Pipe).
func mqttServe(conn net.Conn, cfg mqttConfig, handle func(mqttMessage)) (connected bool, err error) {
	keep := cfg.keepalive
	if keep <= 0 {
		keep = 60 * time.Second
	}
	var wmu sync.Mutex
	write := func(p []byte) error {
		wmu.Lock()
		defer wmu.Unlock()
		conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
		_, err := conn.Write(p)
		return err
	}
	r := bufio.NewReader(conn)

	if err := write(mqttConnectPacket(cfg, keep)); err != nil {
		return false, err
	}
	conn.SetReadDeadline(time.Now().Add(10 * time.Second))
	typ, body, err := mqttRead(r)
	if err != nil {
		return false, fmt.Errorf("connack: %w", err)
	}
	if typ&0xF0 != mqttConnack || len(body) != 2 {
		return false, fmt.Errorf("expected CONNACK, got %#x", typ)
	}
	if body[1] != 0 {
		return false, fmt.Errorf("broker refused the connection: %s", connackReason(body[1]))
	}
	if err := write(mqttSubscribePacket(1, cfg.topic)); err != nil {
		return true, err
	}

	done := make(chan struct{})
	defer close(done)
	go func() { // PINGREQ at half the keep-alive; a dead link then fails the read deadline below
		t := time.NewTicker(keep / 2)
		defer t.Stop()
		for {
			select {
			case <-done:
				return
			case <-t.C:
				if write([]byte{mqttPingreq, 0}) != nil {
					return
				}
			}
		}
	}()

	for {
		conn.SetReadDeadline(time.Now().Add(keep * 3 / 2))
		typ, body, err := mqttRead(r)
		if err != nil {
			return true, err
		}
		switch typ & 0xF0 {
		case mqttSuback:
			if len(body) < 3 || body[2] == 0x80 {
				return true, fmt.Errorf("broker refused the subscription to %q", cfg.topic)
			}
			log.Printf("mqtt: %s: subscribed to %q (QoS %d)", cfg.addr(), cfg.topic, body[2])
		case mqttPublish:
			m, id, err := mqttParsePublish(typ, body)
			if err != nil {
				return true, err
			}
			if id != 0 {
				if err := write([]byte{mqttPuback, 2, byte(id >> 8), byte(id)}); err != nil {
					return true, err
				}
			}
			if len(m.payload) == 0 { // a cleared retained topic (possibly our own clear echoed back)
				continue
			}
			if m.retained && cfg.clear {
				if err := write(mqttPublishPacket(m.topic, nil, true)); err != nil {
					return true, err
				}
			}
			handle(m)
		case mqttPingresp:
		default:
			return true, fmt.Errorf("unexpected packet %#x", typ)
		}
	}
}

func connackReason(rc byte) string {
	switch rc {
	case 1:
		return "unacceptable protocol version"
	case 2:
		return "client id rejected"
	case 3:
		return "server unavailable"
	case 4:
		return "bad user name or password"
	case 5:
		return "not authorised"
	}
	return fmt.Sprintf("code %d", rc)
}

func mqttString(b []byte, s string) []byte {
	b = binary.BigEndian.AppendUint16(b, uint16(len(s)))
	return append(b, s...)
}

// mqttPacket prefixes a body with its fixed header (type byte + variable-length remaining length).
func mqttPacket(typ byte, body []byte) []byte {
	p := []byte{typ}
	n := len(body)
	for {
		b := byte(n % 128)
		n /= 128
		if n > 0 {
			b |= 0x80
		}
		p = append(p, b)
		if n == 0 {
			break
		}
	}
	return append(p, body...)
}

func mqttConnectPacket(cfg mqttConfig, keep time.Duration) []byte {
	flags := byte(0x02) // clean session: never replay a backlog after downtime
	if cfg.user != "" {
		flags |= 0x80
		if cfg.pass != "" {
			flags |= 0x40
		}
	}
	b := mqttString(nil, "MQTT")
	b = append(b, 4, flags) // protocol level 4 = 3.1.1
	b = binary.BigEndian.AppendUint16(b, uint16(keep/time.Second))
	b = mqttString(b, cfg.clientID)
	if cfg.user != "" {
		b = mqttString(b, cfg.user)
		if cfg.pass != "" {
			b = mqttString(b, cfg.pass)
		}
	}
	return mqttPacket(mqttConnect, b)
}

func mqttSubscribePacket(id uint16, topic string) []byte {
	b := binary.BigEndian.AppendUint16(nil, id)
	b = mqttString(b, topic)
	return mqttPacket(mqttSubscribe, append(b, 1)) // QoS 1, as atc-mqtt publishes
}

// mqttPublishPacket builds a QoS 0 PUBLISH (only used to clear a retained topic).
func mqttPublishPacket(topic string, payload []byte, retain bool) []byte {
	typ := byte(mqttPublish)
	if retain {
		typ |= 1
	}
	return mqttPacket(typ, append(mqttString(nil, topic), payload...))
}

func mqttRead(r *bufio.Reader) (typ byte, body []byte, err error) {
	typ, err = r.ReadByte()
	if err != nil {
		return 0, nil, err
	}
	n, mul := 0, 1
	for i := 0; ; i++ {
		b, err := r.ReadByte()
		if err != nil {
			return 0, nil, err
		}
		if i == 4 {
			return 0, nil, errors.New("malformed remaining length")
		}
		n += int(b&0x7F) * mul
		mul *= 128
		if b&0x80 == 0 {
			break
		}
	}
	if n > 1<<20 {
		return 0, nil, fmt.Errorf("packet of %d bytes is too big", n)
	}
	body = make([]byte, n)
	_, err = io.ReadFull(r, body)
	return typ, body, err
}

// mqttParsePublish returns the message and its packet id (0 for QoS 0).
func mqttParsePublish(typ byte, body []byte) (mqttMessage, uint16, error) {
	if len(body) < 2 {
		return mqttMessage{}, 0, errors.New("short PUBLISH")
	}
	tl := int(binary.BigEndian.Uint16(body))
	if len(body) < 2+tl {
		return mqttMessage{}, 0, errors.New("short PUBLISH topic")
	}
	m := mqttMessage{topic: string(body[2 : 2+tl]), retained: typ&1 != 0}
	rest := body[2+tl:]
	var id uint16
	if qos := (typ >> 1) & 3; qos > 0 {
		if len(rest) < 2 {
			return m, 0, errors.New("short PUBLISH id")
		}
		id = binary.BigEndian.Uint16(rest)
		rest = rest[2:]
	}
	m.payload = rest
	return m, id, nil
}
