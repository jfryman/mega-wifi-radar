# CI, releases and publishing

Public repo **github.com/jfryman/mega-wifi-radar** (MIT). One workflow, `.github/workflows/build.yml`,
runs on pushes to `main`, `v*` tags, PRs and manual dispatch. User-facing docs live in `README.md`
and `docs/` (hardware, usage, relay, building). The lode stays the in-depth design record and is published too.

```mermaid
flowchart LR
  subgraph rom[job rom]
    N[scripts/md --name] --> P{pull ghcr .../mega-wifi-radar-sgdk:hash}
    P -- miss --> B[docker build + push if not PR]
    P -- hit --> M
    B --> M[make sim; make release] --> D[dist/: -VER.bin, -VER-emu.bin, SHA256SUMS]
  end
  subgraph relay[job relay]
    T[go vet/test relay + tools] --> X[buildx amd64+arm64<br/>ghcr .../mega-wifi-radar-relay]
  end
  D --> R[job release on v*: gh release create]
  X --> R
  PR[job print: scripts/print<br/>label, cover, manual, booklets] --> R
```

## Contracts
- **SDK image cache:** CI pushes the SGDK image as `ghcr.io/jfryman/mega-wifi-radar-sgdk:<hash>`, using the
  same content hash as `scripts/md`. `scripts/md` pulls that tag before building locally (`MD_REGISTRY=`
  disables this), so users skip the 15–30 min gcc build. CI adds the `org.opencontainers.image.source` label
  at build time instead of in the Dockerfile, which would change the hash.
- **Relay image tags:** `:edge` (main), `:<semver>` and `:latest` (tags), `:pr-N` (PRs, never pushed).
  `relay/Dockerfile` cross-compiles (`FROM --platform=$BUILDPLATFORM`, `GOOS/GOARCH` from `TARGET*`), so arm64 needs no QEMU.
- **Release ROMs never contain credentials:** CI asserts that `src/dev_config.h` is absent. The cart ROM
  boots into setup when no Wi-Fi works or no relay is set.
- **Version:** `make` writes `#define APP_VERSION "<git describe --tags --always --dirty>"` to the git-ignored
  `src/version.h` (only rewritten when it changes). It's included only by `devcfg.c` (always rebuilt), and the
  boot splash status line shows `app_version` (cart builds only; the emulator build omits it). `VERSION=` overrides it; the checkout needs `fetch-depth: 0`.
- **Release assets:** `mega-wifi-radar-<v>.bin`, `-emu.bin`, `-label.pdf`, `-cover.pdf`, `-manual.pdf`,
  `-manual-booklet-letter.pdf`, `-manual-booklet-a4.pdf`, and `SHA256SUMS` covering all of them.
- Order matters: `make sim` wipes `out/release` and `out/rom.bin`, so CI runs it **before** `make release`.
- **GHCR visibility:** both packages are public; anonymous pulls work (checked with a token-less
  `ghcr.io/token` request). The first CI run took ~11 min (it built gcc); runs that hit the cache skip that build.

## Third-party data in the repo
`relay/testdata/*.json` and `res/fake/kbna_seq.bin` are adsb.fi snapshots (personal, non-commercial,
attribution), excluded from MIT by `relay/testdata/NOTICE.md`. `src/airports.c` is from OurAirports (public domain).
Never commit LAN hostnames or IPs; docs use `relay-host.example.lan` / `192.168.1.x` placeholders.

## CHANGELOG.md (mitchellh / HashiCorp style)
The top section is always `## X.Y.Z (Unreleased)`. Every user-visible change adds a bullet there, in
the same commit as the change, under one of these headings: `BACKWARDS INCOMPATIBILITIES:`,
`FEATURES:`, `IMPROVEMENTS:` or `BUG FIXES:`. Bullets look like `  * component: what changed [GH-N]`.
Lode-only and CI-internal changes don't get entries. The release job **fails** if the changelog has no
`## <tag without v> (` section, and uses that section's body as the GitHub release notes.

## Cutting a release
1. Rename `## X.Y.Z (Unreleased)` to `## X.Y.Z (Month D, YYYY)` and add a fresh `## X.Y.(Z+1) (Unreleased)` above it.
2. Commit, then `git tag -a vX.Y.Z -m vX.Y.Z && git push origin main vX.Y.Z`.
Released: **v0.1.0** (2026-10-28).

```sh
# cut a release (after dating the CHANGELOG section)
git tag -a v0.1.3 -m v0.1.3 && git push origin main v0.1.3
gh run watch $(gh run list --branch v0.1.3 -L1 --json databaseId -q '.[0].databaseId')
```

Related: [container-build.md](container-build.md), [../relay/relay.md](../relay/relay.md).
