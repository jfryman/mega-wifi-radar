# Relay ↔ cartridge contract (`src/arframe.h` + `relay/contract_test.go`)

`src/arframe.h` is the **single source of truth** for everything the relay and the cartridge must agree on:
the frame and detail layouts, the version and constants, the HTTP paths, queries and mismatch status, and the
mDNS discovery strings. Two checks chain together, so a change on either side that isn't mirrored fails the
build, and a lock file makes every change bump the version:

```mermaid
flowchart LR
  S[C structs ar_hdr / ar_rec / ar_detail] -- _Static_assert AR_AT offsets<br/>m68k compiler --> H[arframe.h<br/>AR_* defines + AR_AT table]
  H -- relay/contract_test.go<br/>parses the header --> G[Go: consts, encode(), detail.encode(),<br/>parseQuery, versionOK, handlers, mDNS advert]
  H -- sha256, comments and<br/>whitespace stripped --> L[relay/contract.lock<br/>AR_VER n hash]
  G -.-> CI[CI relay job · relay image build · make release]
```

## What is checked
| Item | C side (arframe.h) | Go side | Check |
|---|---|---|---|
| Version | `AR_VER` | `frameVer`, mDNS TXT `v=`, `versionOK` | equal; a request at `AR_VER + 1` gets `AR_HTTP_MISMATCH` |
| Lock | the whole header | `contract.lock` | hash unchanged, or `AR_VER` bumped and the lock re-recorded (`make contract-lock`) |
| Sizes, cap | `AR_HDR_SIZE`, `AR_REC_SIZE_V1`, `AR_MAX_RECS`, `AR_FRAME_MAX`, `AR_DETAIL_SIZE`, `AR_TN`, `AR_TERRAIN_HDR`, `AR_TERRAIN_SIZE`, `AR_LEVELS`, `AR_KEY_LEN`, `AR_TRACK_SIZE`, `AR_STRIP_CELLS`, `AR_NOTICE_SIZE`, `AR_NOTICE_COLS`, `AR_WX_N`, `AR_WX_ROW`, `AR_WX_SIZE`, `AR_MAP_MAX`, `AR_MAP_HDR` | `hdrSize`, `recSize`, `maxRecs`, `detailSize`, `tn`, `terrainHdr`, `terrainSize`, `levels`, `keyLen`, `trackSize`, `noticeSize`, `noticeCols`, `wxN`, `wxRow`, `wxSize`, `mapMax`, `mapHdr` | equal (`FRAME_MAX` = hdr + max·rec, `TERRAIN_SIZE` = hdr + 2·tn²) |
| Flags, sources, sentinels | `AR_H_*`, `AR_F_*`, `AR_D_*`, `AR_T_*`, `AR_K_*`, `AR_N_*`, `AR_W_*`, `AR_M_*`, `AR_SRC_ADSBFI`, `AR_SQUAWK_NONE`, `AR_CAT_NONE`, `AR_HTTP_MISMATCH` | matching consts | equal |
| Layout | every struct (`ar_hdr`, `ar_rec`, `ar_detail`, `ar_terrain`, `ar_track`, `ar_notice`, `ar_wx`, `ar_map`, `ar_seg` (after the map header)), and every field's `AR_AT(struct, field, offset)` + its C type size (array sizes must be literal numbers) | `frame.encode()`, `detail.encode()`, `patch.encode()`, `trackRec.encode()`, `notice.encode()`, `wxGrid.encode()`, `vmap.encode()` | a record with a distinct value in every field, decoded at the C offsets, gives those values back; fields tile the struct with no gaps; an unknown struct fails; the terrain maps follow its header |
| HTTP | `AR_FRAME_*`, `AR_DETAIL_*`, `AR_TERRAIN_*`, `AR_TRACK_*`, `AR_STRIP_*`, `AR_NOTICE_*`, `AR_WX_*`, `AR_MAP_*` paths and queries (the printf formats the cart uses) | mux routes, `parseQuery`, the handlers | paths match; a frame query parses to its values and passes `versionOK`; a detail query returns `AR_DETAIL_SIZE` bytes with `AR_D_FOUND`; a terrain query returns `AR_TERRAIN_SIZE` bytes, a track query with that patch's key puts the aircraft near the patch centre, and a strip query (negative x) returns `2·w·h` bytes; with a notice waiting, frame and track queries at `n` = 0 and id − 1 carry `AR_H_NOTICE` / `AR_K_NOTICE` and the notice query returns it, at `n` = its id neither (zero notice), across an id wrap; a weather query (against a fake IEM) returns `AR_WX_SIZE` bytes with an id, and a frame at `w=0` carries `AR_H_WX`, at `w=<id>` not; a map query returns a header + count (1..`AR_MAP_MAX`) segments |
| Discovery | `AR_MDNS_SERVICE`, `AR_MDNS_QUERY_ID`, `AR_MDNS_SRC_PORT` | `serviceName`, `advert.answer` | the cart's query (built from the defines, byte for byte as `net.c` builds it) gets a legacy answer echoing the id, with TXT `v=AR_VER`, `path=AR_FRAME_PATH` |
| Coverage | any `AR_*` define or `AR_AT` field | — | **must be mapped in the test** (or listed as console-only), so new contract items can't slip past |

```c
/* arframe.h: adding a field means a struct member + an AR_AT line (the C build checks they agree),
 * then the Go test fails until the relay encodes it and the test maps it. */
AR_AT(ar_rec, cs, 16);
#define AR_FRAME_QUERY "?lat=%s&lon=%s&range=%u&max=%u&v=%u"
```

## Lock-step versions
The relay is part of the cartridge: relay vX serves cartridge vX, and both ship from one tag. Any edit to
`arframe.h` beyond comments and whitespace must bump `AR_VER` (TestContractLock: *"arframe.h changed but AR_VER is
still 2: bump AR_VER…"*; after a bump: *"run make contract-lock"*). At runtime:
- every request carries `v=AR_VER`; the relay answers anything else (or none) with 409 and logs both versions;
- the cart treats 409, or a frame/detail whose `ver` isn't `AR_VER`, as `FEED_ERR_VERSION`;
- discovery reads TXT `v=`: a relay at another version is reported as a mismatch, not as *not found*;
- boot probes one frame and holds with `RELAY MISMATCH: RUN RELAY <app_version>`; the radar shows `RELAY VER?`.
`scripts/relay advertise` reads `AR_VER` and `AR_FRAME_PATH` from the header for the host's mDNS TXT, so it can't drift.

Mutation-checked (2026-10-29): a comment edit passes; a value change without a bump fails; a bump without
re-locking fails; a new `AR_*` item or struct fails until mapped.

## Where it runs
- CI `relay` job (`go test ./...` from the checkout). `CI` is set there, so a missing header is a **failure**.
- `make release` / `make sim` run `make contract` first (`go test -run Contract` in `relay/`; it warns and
  continues if Go isn't installed).
- The relay image build (`relay/` context) has no `src/`, so the test **skips** there; CI covers it.
- `make contract-lock` = `CONTRACT_LOCK=update go test -run ContractLock` (writes the lock).

Related: [frame-format.md](frame-format.md), [../net/discovery.md](../net/discovery.md), [../relay/relay.md](../relay/relay.md).
