# Host-side entry points. All compiling happens inside the build image (scripts/md).
MD      := scripts/md
FLASHKIT:= scripts/flashkit
SGDKMK  := make -f /sgdk/makefile.gen

.PHONY: all release debug clean image flash esp-flash sim sim-run relay-image relay-run relay-start relay-stop

all: release

# devcfg.o is always rebuilt: it includes the optional src/dev_config.h, which
# SGDK's dependency tracking can't see appear (see src/devcfg.c).
release:
	rm -f out/release/src/devcfg.o
	$(MD) $(SGDKMK) release

debug:
	rm -f out/debug/src/devcfg.o
	$(MD) $(SGDKMK) debug

clean:
	$(MD) $(SGDKMK) clean

# Emulator build: fake feed (no MegaWiFi), out/sim.bin. Objects are wiped
# before and after so flags never mix with the cart build.
sim:
	rm -rf out/release
	$(MD) $(SGDKMK) release EXTRA_FLAGS=-DFAKE_FEED
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
