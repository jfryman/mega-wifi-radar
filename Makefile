# Host-side entry points. All compiling happens inside the build image (scripts/md).
MD      := scripts/md
FLASHKIT:= scripts/flashkit
SGDKMK  := make -f /sgdk/makefile.gen

.PHONY: all release debug clean image flash esp-flash sim sim-run relay-image relay-run relay-start relay-stop print print-shots splash-art contract contract-lock fake-feed spike-voxel spike-voxel-run spike-voxel-flash

all: release

# Version shown on the boot screen: git describe (tag, commits since, -dirty).
# Written to the git-ignored src/version.h, included only by devcfg.c.
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)

.PHONY: version
version:
	@printf '#define APP_VERSION "%s"\n' "$(VERSION)" > src/version.h.tmp
	@cmp -s src/version.h.tmp src/version.h 2>/dev/null && rm src/version.h.tmp || mv src/version.h.tmp src/version.h

# devcfg.o is always rebuilt: it includes the optional src/dev_config.h, which
# SGDK's dependency tracking can't see appear (see src/devcfg.c).
# Relay <-> cartridge contract (lode/architecture/contract.md): src/arframe.h vs the relay.
# Runs before every ROM build; needs Go on the host (warns and continues without it).
contract:
	@if command -v go >/dev/null 2>&1; then \
		cd relay && go test -count=1 -run Contract ./... >/dev/null || { echo "contract: relay and src/arframe.h disagree:" >&2; go test -count=1 -run Contract ./... >&2; exit 1; }; \
	else echo "contract: go not found, skipping the relay <-> cartridge check (CI still runs it)" >&2; fi

# Emulator feed from the recorded KBNA snapshots: frames, pop-up details, and a cockpit terrain patch
# (real elevation: needs the network) with every aircraft's track (res/fake/, embedded by make sim).
fake-feed:
	cd relay && go run . -pack ../res/fake/kbna_seq.bin -lat 36.1245 -lon -86.6782 -range 25 testdata/kbna-seq/*.json \
		&& go run . -pack-detail ../res/fake/kbna_detail.bin testdata/kbna-seq/*.json \
		&& go run . -pack-cockpit ../res/fake/kbna_cockpit.bin -lat 36.1245 -lon -86.6782 -cell 750 testdata/kbna-seq/*.json

# After bumping AR_VER in src/arframe.h: record the new contract in relay/contract.lock.
contract-lock:
	cd relay && CONTRACT_LOCK=update go test -count=1 -run ContractLock ./... && cat contract.lock

release: version contract
	rm -f out/release/src/devcfg.o
	$(MD) $(SGDKMK) release
	scripts/ram-check out/release/symbol.txt

debug: version
	rm -f out/debug/src/devcfg.o
	$(MD) $(SGDKMK) debug

clean:
	$(MD) $(SGDKMK) clean

# Emulator build: fake feed (no MegaWiFi), out/sim.bin. Objects are wiped
# before and after so flags never mix with the cart build.
sim: version contract
	rm -rf out/release
	$(MD) $(SGDKMK) release EXTRA_FLAGS=-DFAKE_FEED
	scripts/ram-check out/release/symbol.txt
	cp out/rom.bin out/sim.bin
	rm -rf out/release out/rom.bin

SNAP ?= 3,8,15
# Headless MAME run of out/sim.bin; PNG snapshots land in out/snap/.
sim-run: sim
	scripts/sim out/sim.bin $(SNAP)

image:
	$(MD) --image

# FlashKit MD on the host: PATH's flashkit-md, else the pinned release in .tools/.
flash: release
	$(FLASHKIT) write-rom out/rom.bin

# ESP32-C3 on the cart (USB-C): mw-fw-rtos rev-c-1250k (keeps mw_cfg/user_data).
esp-flash:
	scripts/esp-flash

# Relay container (relay/Dockerfile): build runs go vet + tests.
relay-image:
	scripts/relay build

relay-run:
	scripts/relay run

relay-start:
	scripts/relay start

relay-stop:
	scripts/relay stop

# Printed pieces (print/, Typst): label, cover, manual + booklets -> out/print/.
print:
	scripts/print

# Recapture the manual's screenshots (print/shots.txt) from the emulator build in MAME.
print-shots: sim
	scripts/print-shots

# Regenerate the boot splash data (res/splash/*.bin, src/splash_data.h) from res/splash/src/art.typ.
splash-art:
	scripts/splash-art

# Cockpit-mode speed spike (lode/plans/cockpit-mode.md): standalone ROM, spike/voxel/out/rom.bin.
# Terrain: cd tools && go run ./voxelgen -o ../spike/voxel/src
spike-voxel:
	$(MD) $(SGDKMK) -C spike/voxel release

spike-voxel-run: spike-voxel
	scripts/sim spike/voxel/out/rom.bin $(SNAP)

spike-voxel-flash: spike-voxel
	$(FLASHKIT) write-rom spike/voxel/out/rom.bin

# 32X ROM (lode/mars/summary.md): its own image (mars/Dockerfile via scripts/mars). The Genesis build above
# doesn't use any of it. out/mars/cart/mwradar.32x (relay feed), out/mars/emu/mwradar.32x (recorded demo).
.PHONY: mars mars-sim mars-sim-run mars-flash print-32x print-shots-32x mars-image mars-spike mars-spike-flash mars-spike-sim mars-clean
RELAY ?=
MARS_BIOS ?= $(CURDIR)/bios/32x

mars: version contract
	scripts/mars make -C mars/rom

mars-sim: version
	scripts/mars make -C mars/rom FAKE=1

# Headless MAME run of the 32X demo ROM (needs your 32X BIOS dumps in bios/32x/); PNGs in out/snap/32x/.
mars-sim-run: mars-sim
	MARS_BIOS=$(MARS_BIOS) scripts/mars-sim out/mars/emu/mwradar.32x $(SNAP)

# The 32X edition's manual screenshots (print/shots-32x.txt) from the demo ROM in MAME.
print-shots-32x: mars-sim
	MARS_BIOS=$(MARS_BIOS) scripts/print-shots 32x

# The 32X edition's printed pieces -> out/print/32x/.
print-32x:
	EDITION=32x scripts/print

mars-flash: mars
	$(FLASHKIT) write-rom out/mars/cart/mwradar.32x

mars-image:
	scripts/mars --image

# out/mars/spike.32x. RELAY=host:port also makes it GET /healthz from that relay.
mars-spike:
	scripts/mars make -C mars/spike RELAY=$(RELAY) BUILD=$(VERSION)

mars-spike-flash: mars-spike
	$(FLASHKIT) write-rom out/mars/spike.32x

# Headless MAME with the 32X BIOS dumps in bios/32x/ (git-ignored; MARS_BIOS=dir overrides, see scripts/mars-sim).
mars-spike-sim: mars-spike
	MARS_BIOS=$(MARS_BIOS) scripts/mars-sim out/mars/spike.32x $(SNAP)

mars-clean:
	scripts/mars make -C mars/spike clean
	rm -rf out/mars/cart out/mars/emu
