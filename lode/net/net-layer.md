# Network layer (src/net.c)

Blocking MegaWiFi helpers for start-up and setup screens. The radar loop must **not** call them;
the non-blocking path (async API or `mw_send`/`mw_recv`) is still to be designed.

```c
net_init();                                   /* mw_init(cmd_buf, MW_BUFLEN) + TSK_userSet(user_tsk) */
net_detect(&mod);                             /* mw_detect -> fw major.minor + variant */
net_ensure_ap(ssid, pass);                    /* slot 0, DHCP, default, mw_cfg_save; no-op if unchanged */
net_join(ip);                                 /* mw_ap_assoc(def slot) + wait 30 s + mw_ip_current */
net_http_get(url, chunk_cb, ctx, &res);       /* url/method/open(0)/finish, then recv chunks on MW_HTTP_CH */
```

```mermaid
sequenceDiagram
  participant M as main (supervisor)
  participant U as user_tsk: mw_process()
  participant E as ESP32
  M->>E: mw_http_url_set / method / open(0)
  M->>E: mw_http_finish (blocks up to 20 s)
  E-->>M: status + content_len
  loop until received == content_len
    M->>U: mw_recv_sync(MW_HTTP_CH) (blocks, user task pumps UART)
    E-->>M: chunk ≤ MW_BUFLEN (1500)
    M->>M: chunk_cb(data, len)
  end
```

## Contracts
- `cmd_buf` (`MW_BUFLEN` = 1500 B) is shared by commands and body chunks. A chunk pointer is only
  valid inside the callback.
- `net_ensure_ap` writes ESP flash only when the SSID, password or default slot differ, so it doesn't wear flash on every boot.
- IP `0.0.0.0` in `mw_ip_cfg` means DHCP (firmware `megawifi.c`).
- The body loop needs a real `Content-Length`, so the relay must send it (no chunked encoding).
- Timeouts: assoc 30 s, HTTP finish 20 s, per-chunk receive 10 s (`MW_MS_TO_FRAMES`).
- `res.frames` (VBlanks from open to last byte) is the latency/throughput metric.

## Spike ROM (src/main.c)
Detect → `net_ensure_ap(devcfg_wifi_ssid…)` → join → GET `devcfg_http_url` (default `http://example.com/`)
→ prints the status, length, chunk count, frames, and the first ~15 lines of the body. Then a spinner
proves the main loop is alive.

### Dev settings
The git-ignored `src/dev_config.h` (template: `src/dev_config.h.example`) is included **only** by
`src/devcfg.c`, which exposes `devcfg_wifi_ssid` / `devcfg_wifi_pass` (NULL = use the module's saved
AP) and `devcfg_http_url`. The host `make` deletes `devcfg.o` before every build.

```c
#if __has_include("dev_config.h")   /* SGDK's .d files never list a header that was absent */
#include "dev_config.h"             /* at the last build, so creating it later wouldn't    */
#endif                              /* trigger a rebuild. Hence the forced devcfg.o rebuild. */
```
Lesson: any `__has_include`d optional file must sit behind a forced-rebuild object like this.
Wi-Fi credentials end up in `out/rom.bin`. `out/` is git-ignored; don't share dev ROMs.

`src/rom_header.c` overrides SGDK's: system `"SEGA MEGAWIFI   "` and **no SRAM** (SGDK's default
declares SRAM at `$200000`, which is ROM on Rev.C).

Related: [../platform/sgdk-megawifi.md](../platform/sgdk-megawifi.md), [../architecture/data-path-options.md](../architecture/data-path-options.md).
