# Practices

## Workflow
- Explore and design first, then implement. Record each decision in the lode before writing code.
- Every script and build step must run on **macOS and Arch Linux (Omarchy)**, never Windows-only.
  Use Podman/Docker for toolchains that aren't packaged natively on both.
- Develop rendering in emulators against the **fake source**. Test networking only on the real cart.

## Hard rules for 68k code
- No floats. Use `fix16`/`fix32` and lookup tables. Do geographic projection off-console.
- Never touch the VDP from the user task (`mw_process()` context).
- Never call a blocking `mw_*` function in the radar loop. Blocking calls belong to start-up/setup screens only.
- Respect the sprite budget: 80 total, 20 per scanline.
- ROM size ≤ 4 MiB unless there is a reason to go bigger (emulators and tools assume 4 MiB).

## SGDK library must match Rev.C
The UART clock and baud rate are compiled into `libmd.a`. Editing our own sources alone does nothing.

```c
/* inc/ext/mw/16c550.h, as patched for Rev.C */
#define UART_CLK  20000000LU   /* stock: 24000000LU */
#define UART_BR    1250000LU   /* stock: 1500000LU  -> divisor stays 1 */
```

```mermaid
flowchart LR
  P[patch 16c550.h + config.h] --> L[rebuild libmd.a] --> R[build ROM] --> F[flashkit-md write-rom]
```

Related: [platform/sgdk-megawifi.md](platform/sgdk-megawifi.md), [platform/hardware.md](platform/hardware.md).
