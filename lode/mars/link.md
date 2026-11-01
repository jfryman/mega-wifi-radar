# 32X COMM link (68000 <-> slave SH-2)

The 68000 and the SH-2s share no memory: the only path is the 32X's eight COMM registers. The slave SH-2 is an RPC
client; the 68000 serves one request at a time, blocking, exactly where the Genesis main loop blocked. Code:
`mars/rom/link.h` (shared), `mars/rom/md/link.c`, `mars/rom/sh/link.c`. Architecture: [summary.md](summary.md).

## Registers (byte offsets from COMM0: `$A15120` on the 68000, `$20004020` on the SH-2s)
| Reg | Writer | Use |
|---|---|---|
| COMM0 | slave | control: low byte = its sequence (when sending) or ack (when receiving) |
| COMM2, 4, 6 | slave | 6 payload bytes, slave -> 68000 |
| COMM8 | 68000 | control: low byte = its sequence / ack; high byte = link state |
| COMM10, 12 | 68000 | 4 payload bytes, 68000 -> slave |
| COMM14 | 68000 | pad 1, SGDK `BUTTON_*` bits, updated every frame (also while blocked in network calls) |

COMM0-7 carry the boot handshake ("M_OK" / "S_OK") first; the link starts once the slave runs, with both sequences 0.

## Blocks
Half duplex. A writer W and a reader R: W fills its payload registers, then sets its sequence to R's ack + 1
(mod 256); R sees a sequence different from its ack, reads the payload, and sets its ack to it; W waits for that ack
before the next block. A message is a 2-byte length then the bytes, padded to whole blocks.
```c
/* SH-2 -> 68000, one block (sh/link.c) */
COMM(2) = w0; COMM(4) = w1; COMM(6) = w2;
seq = (COMM(8) & 0xFF) + 1;  COMM(0) = seq;        /* 68000's ack + 1 */
while ((COMM(8) & 0xFF) != seq) idle();            /* acked */
```
~6 register accesses per block: tens of KB/s, well above the UART's real rate (the Genesis measured ~13 KB/s for
terrain).

## RPCs
Request: `u8 cmd, u8 0, args`. Reply: `u8 status, u8 0, data`. The data layouts are the structs of `src/arframe.h`,
`src/feed.h`, `src/net.h` and `src/settings.h` (both CPUs are big-endian; the shared headers' `_Static_assert`s pin
the layouts). Commands (`enum lk_cmd` in `link.h`): `HELLO`, `SETTINGS` (push the slave's `struct settings` before
any feed call), `FEED_FETCH`, `FEED_DETAIL`, `FEED_TERRAIN`, `FEED_TRACK`, `FEED_STRIP`, `FIND_RELAY`,
`RECONNECT_DUE`, `RECONNECT`, `RELAY_URL`, `NET_*` (init, detect, ensure AP, join, scan, connect, current SSID,
rejoin needed), `FLASH_READ` / `FLASH_ERASE` / `FLASH_WRITE`, `AP_DISASSOC`, `FACTORY`.

Related: [summary.md](summary.md), [../net/feed.md](../net/feed.md), [../net/net-layer.md](../net/net-layer.md).
