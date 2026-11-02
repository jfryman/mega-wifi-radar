# Test data notice

`kbna-25nm.json` and `kbna-seq/*.json` are snapshots of aircraft within 25 nm of KBNA, taken
from the [adsb.fi](https://adsb.fi) open data API on 2026-10-28. `res/fake/kbna_seq.bin` (the
emulator build's recorded feed) is generated from them with `relay -pack`.

`arrivals/*.jsonl.gz` are recordings (`relay -record`, one snapshot a second, everything within 12 nm of KBNA)
of real arrivals at KBNA on 2026-11-02, each cut (`relay -cut`) from ~4,000 ft to a minute after touchdown:
SWA3092 (B38M) on 20L and SCX3003 (B738) on 20R. `relay -replay` serves one as a live upstream.

adsb.fi open data is provided for personal, non-commercial use with attribution
("Data: adsb.fi"). These files are included only as test fixtures and as an offline demo feed.
They are **not** covered by this repository's MIT license.
