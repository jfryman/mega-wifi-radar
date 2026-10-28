# Network layer (src/net.c)

Blocking MegaWiFi helpers for start-up and setup screens. The radar loop must **not** call them;
the non-blocking path (async API or `mw_send`/`mw_recv`) is still to be designed.

```c
net_init();                                   /* mw_init(cmd_buf, MW_BUFLEN) + TSK_userSet(user_tsk) */
net_detect(&mod);                             /* mw_detect -> fw major.minor + variant (5 tries x 2 s) */
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
- **`TSK_userSet(user_tsk)` must come before `mw_init()`.** SGDK's `uart_start()` times the ESP32
  reset with `TSK_superPend()`, which returns *immediately* when no user task is set
  (`tst.l task_pc; beq no_task` in `task.s`). Without the 30 ms + 1 s waits, RTS goes active
  while the chip is still booting. On Rev.C ~RTS drives GPIO9, the boot strap, so the ESP32 boots
  into **download mode**: `mw_detect` fails, and the module, which had booted normally at power-on, "goes off".
  SGDK's own `sample/megawifi/basic` uses the wrong order.
- **`user_tsk` needs a compiler memory barrier in its loop.** With `-flto -O3`, `mw_process()` is
  inlined into the infinite loop, and gcc hoists loads of SGDK's non-volatile globals
  (`activeCommType`, the lsd `d.rx.stat`) to task entry. Because the task starts before `mw_init()`,
  it cached a NULL driver and never serviced the UART: replies sat in the RX FIFO and every
  `mw_detect` timed out. That only showed on **cold boots**. A soft reset doesn't clear RAM, so the
  stale pointer from the last run happened to be valid. Verify with
  `m68k-elf-objdump -d out/release/rom.out` that `activeCommType` is loaded *inside* the loop.
  ```c
  while (TRUE) {
      __asm__ volatile ("" ::: "memory");   /* re-read SGDK state every pass */
      if (mw_up) mw_process();
  }
  ```
  Debugging playbook for "module not found": (1) bring-up ROM on a cold boot (hardware/firmware);
  (2) time `mw_init`/`mw_detect` in frames and read LSR/MSR (reset waits about 62 frames; CTS = MSR bit 0x10);
  (3) send a raw LSD VERSION frame `7E 00 04 00 01 00 00 7E` straight to the 16C550 and dump the
  reply. A good reply is `7E 00 0B 00 00 00 07 01 05 01 …`. If the raw frame works and SGDK's path
  doesn't, check the disassembly.
- Soft reset keeps statics: anything that must start fresh is set at the top of `main` (e.g. `line = 1`).
- `user_tsk` idles until the `mw_up` flag is set after `mw_init()` returns, because lsd/comm state isn't valid before then.
- `cmd_buf` (`MW_BUFLEN` = 1500 B) is shared by commands and body chunks. A chunk pointer is only
  valid inside the callback.
- `net_ensure_ap` writes ESP flash only when the SSID, password or default slot differ, so it doesn't wear flash on every boot.
- IP `0.0.0.0` in `mw_ip_cfg` means DHCP (firmware `megawifi.c`).
- Body length: with a `Content-Length`, the loop reads exactly that many bytes. On a chunked response
  the firmware reports `INT32_MAX` (`HTTP_LEN_CHUNKED`) and ends the body with a **zero-length frame**
  on `MW_HTTP_CH`, so the loop stops there (`res.chunked`). Without that, chunked bodies (example.com
  sends 577 B chunked) hang until the 10 s receive timeout. The relay still always sends
  `Content-Length`, so the console can check the frame size before parsing.
- Timeouts: assoc 30 s, HTTP finish 20 s, per-chunk receive 10 s (`MW_MS_TO_FRAMES`).
- Any failed request ends with `mw_http_cleanup()`. Otherwise the firmware's client stays in a busy
  state (e.g. `FINISH_CONTENT_WAIT`) and refuses the next `mw_http_url_set`. After a successful request
  it's back to `IDLE` and is reused as-is (firmware `main/http.c`).
- `res.frames` (VBlanks from open to last byte) is the latency/throughput metric.

## Spike ROM (src/main.c)
Detect → `net_ensure_ap(devcfg_wifi_ssid…)` → join → poll the relay through the feed every ~5 s and
list the nearest aircraft; see [feed.md](feed.md).

### Relay discovery
`net_discover(url)`: one mDNS legacy-unicast query on socket channel 1 (UDP reuse mode). See [discovery.md](discovery.md).

### Dev settings
The git-ignored `src/dev_config.h` (template: `src/dev_config.h.example`) is included **only** by
`src/devcfg.c`, which exposes `devcfg_wifi_ssid` / `devcfg_wifi_pass` (NULL = use the module's saved
AP) and `devcfg_relay_url` (relay base URL, NULL = not configured). The host `make` deletes `devcfg.o` before every build.

```c
#if __has_include("dev_config.h") && !defined(FAKE_FEED)  /* SGDK's .d files never list a */
#include "dev_config.h"             /* at the last build, so creating it later wouldn't    */
#endif                              /* trigger a rebuild. Hence the forced devcfg.o rebuild. */
```
Home point: `devcfg_home_icao`, `devcfg_home_lat_e4`, `devcfg_home_lon_e4` (s32, 1e-4 deg) and
`devcfg_range_nm` default to **KBNA, 36.1245, -86.6782, 25 nm** (OurAirports), overridable
with `DEV_HOME_ICAO` / `_LAT_E4` / `_LON_E4` / `DEV_RANGE_NM`. These are the values the relay
query (`lat=…&lon=…&range=…`) is built from. `main.c`'s `fmt_deg_e4()` prints them with no floats.

Lesson: any `__has_include`d optional file must sit behind a forced-rebuild object like this.
Wi-Fi credentials end up in `out/rom.bin`. `out/` is git-ignored; don't share dev ROMs.
The **emulator build (`FAKE_FEED`) never includes it**: its screenshots go into the manual, and a dev relay
hostname once showed up on the setup screen. `devcfg.c` also includes the generated `version.h` the same way.

`src/rom_header.c` overrides SGDK's: system `"SEGA MEGAWIFI   "` and **no SRAM** (SGDK's default
declares SRAM at `$200000`, which is ROM on Rev.C).

Related: [../platform/sgdk-megawifi.md](../platform/sgdk-megawifi.md), [../architecture/data-path-options.md](../architecture/data-path-options.md).
