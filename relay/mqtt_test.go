package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"flag"
	"net"
	"testing"
	"time"
)

// A fake broker on the other end of a pipe: checks CONNECT and SUBSCRIBE, sends one QoS 1 retained
// PUBLISH and expects the PUBACK, then the clear (an empty retained PUBLISH) when cfg.clear is set.
func TestMQTTSession(t *testing.T) {
	for _, clear := range []bool{false, true} {
		cli, brk := net.Pipe()
		cfg := mqttConfig{host: "broker", port: 1883, user: "u", pass: "p", topic: "atc/status",
			clientID: "test-client", clear: clear, keepalive: 30 * time.Second}
		got := make(chan mqttMessage, 1)
		done := make(chan error, 1)
		go func() {
			_, err := mqttServe(cli, cfg, func(m mqttMessage) { got <- m })
			done <- err
		}()
		r := bufio.NewReader(brk)
		brk.SetDeadline(time.Now().Add(5 * time.Second))

		typ, body, err := mqttRead(r)
		if err != nil || typ != mqttConnect {
			t.Fatalf("CONNECT: %#x %v", typ, err)
		}
		want := mqttString(nil, "MQTT")
		want = append(want, 4, 0x02|0x80|0x40, 0, 30)
		want = mqttString(want, "test-client")
		want = mqttString(mqttString(want, "u"), "p")
		if !bytes.Equal(body, want) {
			t.Fatalf("CONNECT body\n got %x\nwant %x", body, want)
		}
		brk.Write([]byte{mqttConnack, 2, 0, 0})

		typ, body, err = mqttRead(r)
		if err != nil || typ != mqttSubscribe {
			t.Fatalf("SUBSCRIBE: %#x %v", typ, err)
		}
		if want := append(mqttString([]byte{0, 1}, "atc/status"), 1); !bytes.Equal(body, want) {
			t.Fatalf("SUBSCRIBE body %x, want %x", body, want)
		}
		brk.Write([]byte{mqttSuback, 3, 0, 1, 1})

		payload := []byte(`{"entering_flights":[]}`)
		pub := binary.BigEndian.AppendUint16(mqttString(nil, "atc/status"), 0x1234)
		brk.Write(mqttPacket(mqttPublish|0x02|0x01, append(pub, payload...))) // QoS 1, retained

		typ, body, err = mqttRead(r)
		if err != nil || typ != mqttPuback || !bytes.Equal(body, []byte{0x12, 0x34}) {
			t.Fatalf("PUBACK: %#x %x %v", typ, body, err)
		}
		if clear {
			typ, body, err = mqttRead(r)
			if err != nil || typ != mqttPublish|1 || !bytes.Equal(body, mqttString(nil, "atc/status")) {
				t.Fatalf("clear: %#x %x %v", typ, body, err)
			}
		}
		select {
		case m := <-got:
			if m.topic != "atc/status" || !m.retained || !bytes.Equal(m.payload, payload) {
				t.Fatalf("message %+v", m)
			}
		case <-time.After(time.Second):
			t.Fatal("handler not called")
		}
		brk.Close()
		if err := <-done; err == nil {
			t.Fatal("session should end with an error when the broker goes away")
		}
	}
}

func TestMQTTRefused(t *testing.T) {
	cli, brk := net.Pipe()
	go func() {
		mqttRead(bufio.NewReader(brk))
		brk.Write([]byte{mqttConnack, 2, 0, 4})
	}()
	ok, err := mqttServe(cli, mqttConfig{topic: "t", clientID: "c"}, func(mqttMessage) {})
	if ok || err == nil || !bytes.Contains([]byte(err.Error()), []byte("bad user name or password")) {
		t.Fatalf("got %v %v", ok, err)
	}
}

func TestMQTTRemainingLength(t *testing.T) {
	for _, n := range []int{0, 127, 128, 16383, 16384, 200000} {
		p := mqttPacket(mqttPublish, make([]byte, n))
		typ, body, err := mqttRead(bufio.NewReader(bytes.NewReader(p)))
		if err != nil || typ != mqttPublish || len(body) != n {
			t.Fatalf("%d: %#x %d %v", n, typ, len(body), err)
		}
	}
}

func TestParseATC(t *testing.T) {
	status := []byte(`{"timestamp":"2025-07-01T14:30:00Z",
		"entering_flights":[{"flight_uuid":"a","flight_number":"SW1234","airline":"Southwest Airlines",
			"aircraft_type":"Boeing 737-800","origin":"BNA","destination":"MDW","heading":90,
			"altitude_feet":1800,"latitude":36.12,"longitude":-86.67,"ground_speed_knots":160,
			"timestamp":"2025-07-01T14:30:00Z","event_type":"entering"}],
		"exiting_flights":[{"flight_uuid":"b","flight_number":"DL1","event_type":"exiting"}],
		"tracked_flights":[],"summary":{"entering_count":1,"exiting_count":1,"currently_tracked_count":1}}`)
	fs, err := parseATC(status)
	if err != nil || len(fs) != 1 || fs[0].Number != "SW1234" || fs[0].AltFt != 1800 || fs[0].Destination != "MDW" {
		t.Fatalf("status: %+v %v", fs, err)
	}
	fs, err = parseATC([]byte(`{"flight_uuid":"c","flight_number":"AA9","event_type":"entering"}`))
	if err != nil || len(fs) != 1 || fs[0].Number != "AA9" {
		t.Fatalf("legacy incoming: %+v %v", fs, err)
	}
	if fs, _ := parseATC([]byte(`{"flight_uuid":"c","event_type":"exiting"}`)); len(fs) != 0 {
		t.Fatalf("legacy outgoing should give nothing: %+v", fs)
	}
	if _, err := parseATC([]byte(`{"entering_flights":[],"error":"API connection failed"}`)); err == nil {
		t.Fatal("error message should be an error")
	}
	if _, err := parseATC([]byte(`not json`)); err == nil {
		t.Fatal("bad JSON should be an error")
	}
}

func TestEnvFlags(t *testing.T) {
	fs := flag.NewFlagSet("t", flag.ContinueOnError)
	host := fs.String("mqtt-host", "", "")
	port := fs.Int("mqtt-port", 1883, "")
	topic := fs.String("mqtt-topic", "atc/status", "")
	t.Setenv("MQTT_BROKER_HOST", "mosquitto.svc")
	t.Setenv("MQTT_BROKER_PORT", "1884")
	t.Setenv("MQTT_TOPIC", "")
	envFlags(fs, map[string]string{"mqtt-host": "MQTT_BROKER_HOST", "mqtt-port": "MQTT_BROKER_PORT", "mqtt-topic": "MQTT_TOPIC"})
	fs.Parse([]string{"-mqtt-port", "1885"})
	if *host != "mosquitto.svc" || *port != 1885 || *topic != "atc/status" {
		t.Fatalf("env < flag, empty env = default: got %q %d %q", *host, *port, *topic)
	}
}
