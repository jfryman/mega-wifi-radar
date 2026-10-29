# Relay ↔ cartridge contract (`src/arframe.h` + `relay/contract_test.go`)

`src/arframe.h` is the **single source of truth** for everything the relay and the cartridge must agree on:
the frame layout, version and constants, the HTTP path and query, and the mDNS discovery strings. Two
checks chain together, so a change on either side that isn't mirrored fails the build:

```mermaid
flowchart LR
  S[C structs ar_hdr / ar_rec] -- _Static_assert AR_AT offsets<br/>m68k compiler --> H[arframe.h<br/>AR_* defines + AR_AT table]
  H -- relay/contract_test.go<br/>parses the header --> G[Go: consts, encode(), parseQuery,<br/>mux path, mDNS advert]
  G -.-> CI[CI relay job · relay image build · make release]
```

## What is checked
| Item | C side (arframe.h) | Go side | Check |
|---|---|---|---|
| Version | `AR_VER` | `frameVer`, mDNS TXT `v=` | equal |
| Sizes, cap | `AR_HDR_SIZE`, `AR_REC_SIZE_V1`, `AR_MAX_RECS`, `AR_FRAME_MAX` | `hdrSize`, `recSize`, `maxRecs` | equal (`FRAME_MAX` = hdr + max·rec) |
| Flags, sources, sentinels | `AR_H_*`, `AR_F_*`, `AR_SRC_ADSBFI`, `AR_SQUAWK_NONE`, `AR_CAT_NONE` | matching consts | equal |
| Layout | every field's `AR_AT(struct, field, offset)` + its C type size | `frame.encode()` | a frame with a distinct value in every field, decoded at the C offsets, gives those values back; fields tile the struct with no gaps |
| HTTP | `AR_FRAME_PATH`, `AR_FRAME_QUERY` (the printf format the cart uses) | mux route, `parseQuery` | the route matches; a query built from the format parses to the values it was built from |
| Discovery | `AR_MDNS_SERVICE`, `AR_MDNS_QUERY_ID`, `AR_MDNS_SRC_PORT` | `serviceName`, `advert.answer` | the cart's query (built from the defines, byte for byte as `net.c` builds it) gets a legacy answer echoing the id, with TXT `v=AR_VER`, `path=AR_FRAME_PATH` |
| Coverage | any `AR_*` define or `AR_AT` field | — | **must be mapped in the test** (or listed as console-only), so new contract items can't slip past |

```c
/* arframe.h: adding a field means a struct member + an AR_AT line (the C build checks they agree),
 * then the Go test fails until the relay encodes it and the test maps it. */
AR_AT(ar_rec, cs, 16);
#define AR_FRAME_QUERY "?lat=%s&lon=%s&range=%u&max=%u"
```

## Where it runs
- CI `relay` job (`go test ./...` from the checkout). `CI` is set there, so a missing header is a **failure**.
- `make release` / `make sim` run `make contract` first (`go test -run Contract` in `relay/`; it warns and
  continues if Go isn't installed).
- The relay image build (`relay/` context) has no `src/`, so the test **skips** there; CI covers it.

Related: [frame-format.md](frame-format.md), [../net/discovery.md](../net/discovery.md), [../relay/relay.md](../relay/relay.md).
