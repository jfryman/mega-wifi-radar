# Data path options

The digester turns ADS-B JSON into compact frames. The compact frame format must be identical in
both options, so the logic can move from A to B without changing the ROM.

```mermaid
flowchart LR
  subgraph A[Option A: relay]
    API1[ADS-B HTTPS] --> R[relay Pi/VPS] -- plain HTTP/TCP --> E1[ESP32]
  end
  subgraph B[Option B: firmware]
    API2[ADS-B HTTPS] --> E2[ESP32 fetch + cJSON + digest]
  end
  E1 --> MD[68k]
  E2 --> MD
```

| | A: Relay | B: Firmware extension |
|---|---|---|
| Iteration speed | fast (Python/Go) | slow (IDF rebuild + reflash) |
| Standalone cart | no (needs a server) | yes |
| HTTPS problem | avoided | needs the cert-bundle patch ([firmware.md](../platform/firmware.md)) |
| Fork divergence | none | new command in mw-fw-rtos |

Owner's lean: **start with A** (relay), and keep B possible. Path A is what's being built.

## Frame format
Agreed v1: [frame-format.md](frame-format.md) (16-byte header + 24-byte records, 1/32 nm,
nearest-first, cap 96). It's identical for A and B.

Related: [../data/adsb-sources.md](../data/adsb-sources.md), [../plans/roadmap.md](../plans/roadmap.md).
