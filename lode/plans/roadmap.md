# Roadmap

```mermaid
flowchart TD
  S1[1. Pin SGDK + reproducible Rev.C patch] --> S2[2. Network spike: detect, assoc, HTTP GET, print]
  S2 --> S3[2b. Relay prototype serving compact frame]
  S1 --> S4[3. Agree frame format + screen layout]
  S4 --> S5[3b. Renderer vs fake source in emulator]
  S3 --> S6[Live radar on real cart]
  S5 --> S6
  S6 --> S7[Network setup screen, settings in ESP flash]
```

## Open decisions
- [ ] SGDK pin: v2.11 tag vs master commit (see [../platform/sgdk-megawifi.md](../platform/sgdk-megawifi.md)).
- [ ] How to deliver the toolchain on macOS + Arch: container image vs native install.
- [ ] Patch mechanism: patch script over a pinned checkout vs vendored submodule fork.
- [ ] Data path A vs B ([../architecture/data-path-options.md](../architecture/data-path-options.md)).
- [ ] Compact frame field ranges (altitude above FL255? ground speed resolution?).
- [ ] Transport for the frame: HTTP GET via `MW_HTTP_CH` vs raw TCP socket.
- [ ] Screen layout and the sweep technique.
- [ ] Git repository: not initialised yet.
