package main

// Flyover notices from the owner's atc-mqtt service (lode/plans/flyover-notices.md).

import (
	"encoding/json"
	"errors"
	"log"
)

// atcFlight is one flight object from atc-mqtt (its INTERFACE.md). Missing strings arrive as "Unknown".
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

// noticeHandler turns MQTT messages into notices. For now it logs what it would announce.
func noticeHandler(m mqttMessage) {
	flights, err := parseATC(m.payload)
	if err != nil {
		log.Printf("mqtt: %s: %v", m.topic, err)
		return
	}
	for _, f := range flights {
		log.Printf("mqtt: %s: entering %s (%s %s, %s > %s, %.0f ft, %.0f kt)", m.topic, f.Number,
			f.Airline, f.Type, f.Origin, f.Destination, f.AltFt, f.GsKt)
	}
}
