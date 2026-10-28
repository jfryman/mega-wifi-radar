# Hardware: Mega Drive + MegaWiFi Rev.C

## Console
- 68000 @ ~7.67 MHz NTSC, no FPU, 16-bit `MULU/DIVU` only.
- 64 KiB work RAM (SGDK uses part of it).
- VDP: H40 320×224, 64 KiB VRAM, 8×8 tiles, planes A/B + window, 80 sprites (20/line),
  4 palettes × 15 colours + backdrop, 9-bit colour. No framebuffer.

## Cartridge (github.com/jfryman/mw-cart)
| Part | Detail |
|---|---|
| ROM | 8 MiB S29GL064N mapped flat `$000000–$7FFFFF` (R1 ties `~CART_IN`→A22; Mega-CD unreachable) |
| Wi-Fi | ESP32-C3-WROOM-02, 2.4 GHz, ~400 KB SRAM, 4 MB flash |
| UART | TL16C550D @ `$A130C1`, regs at `+2n`, **20 MHz clock, 1.25 Mbaud, RTS/CTS** |
| Storage | No SRAM. Settings go in ESP32 user flash (`mw_flash_*`, 4 KiB sectors); 3 Wi-Fi slots in module NV |

```mermaid
flowchart LR
  M68K[68000] -- odd bytes $A130C1 --> UART[TL16C550D<br/>20 MHz]
  UART -- 1.25M RTS/CTS --> ESP[ESP32-C3<br/>fw rev-c-1250k]
  ESP -- USB-C --> HOST[esptool flashing]
```

## Invariants
- Both ends must agree on 1.25 Mbaud. Stock SGDK (24 MHz/1.5 M) and stock firmware won't talk to Rev.C.
- Divisor = 20 000 000 / (16 × 1 250 000) = **1**, so the rate is exact.
- Fresh boards have no Wi-Fi configured, so the ROM needs its own network setup screen.

## Flashing
```sh
esptool.py --chip esp32c3 -p /dev/cu.usbmodem* write_flash 0x0 mw-fw-rtos-merged.bin  # ESP32 (once)
flashkit-md write-rom out/rom.bin                                                     # ROM (FlashKit ≥ v1.10.5)
```
Device nodes: FlashKit = `/dev/cu.usbserial-*` / `/dev/ttyUSB*`; ESP32 = `/dev/cu.usbmodem*` / `/dev/ttyACM*`.

Related: [sgdk-megawifi.md](sgdk-megawifi.md), [firmware.md](firmware.md).
