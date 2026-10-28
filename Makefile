# Host-side entry points. All compiling happens inside the build image (scripts/md).
MD      := scripts/md
FLASHKIT:= scripts/flashkit
SGDKMK  := make -f /sgdk/makefile.gen

.PHONY: all release debug clean image flash esp-flash relay-image relay-run relay-start relay-stop

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
