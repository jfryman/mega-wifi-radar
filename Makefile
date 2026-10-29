# Host-side entry points. All compiling happens inside the build image (scripts/md).
MD      := scripts/md
FLASHKIT:= scripts/flashkit
SGDKMK  := make -f /sgdk/makefile.gen

.PHONY: all release debug clean image flash esp-flash sim sim-run relay-image relay-run relay-start relay-stop print print-shots splash-art contract contract-lock fake-feed

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

# Emulator feed from the recorded KBNA snapshots: frames + pop-up details (res/fake/, embedded by make sim).
fake-feed:
	cd relay && go run . -pack ../res/fake/kbna_seq.bin -lat 36.1245 -lon -86.6782 -range 25 testdata/kbna-seq/*.json \
		&& go run . -pack-detail ../res/fake/kbna_detail.bin testdata/kbna-seq/*.json

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
