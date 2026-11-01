# 32X MegaWiFi spike: one cartridge image built from a 68000 program (md/) and an SH-2 program (sh/).
# Runs inside the 32X build image (scripts/mars); from the repo root use `make mars-spike`.
#   make RELAY=192.168.1.20:8080   # also GET /healthz from a relay at that address
MD      = m68k-elf-
SH      = sh-elf-
BOOT   ?= $(MARS_BOOT)
OUT    ?= ../../out/mars
ROM     = $(OUT)/spike.32x
ROMSIZE = 262144

RELAY  ?=
RELAY_HOST = $(word 1,$(subst :, ,$(RELAY)))
RELAY_PORT = $(or $(word 2,$(subst :, ,$(RELAY))),8080)
RELAY_DEFS = $(if $(RELAY),-DRELAY_HOST='"$(RELAY_HOST)"' -DRELAY_PORT='"$(RELAY_PORT)"')

COMMON  = -Os -Wall -Wextra -ffreestanding -fno-builtin -fomit-frame-pointer \
          -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables
MDFLAGS = -m68000 $(COMMON)
SHFLAGS = -m2 -mb $(COMMON)

MDOBJS  = $(OUT)/md/boot.o $(OUT)/md/main.o $(OUT)/md/uart_ram.o $(OUT)/md/vdp.o $(OUT)/md/font.o
SHOBJS  = $(OUT)/sh/start.o $(OUT)/sh/main.o

all: $(ROM) check

# ---------------------------------------------------------------- 68000 program -> md.bin
$(OUT)/md/%.o: md/%.c md/*.h shared.h Makefile | $(OUT)/md
	$(MD)gcc $(MDFLAGS) $(RELAY_DEFS) -c $< -o $@

$(OUT)/md/main.o: $(OUT)/relay.stamp

$(OUT)/md/boot.o: md/boot.s | $(OUT)/md
	@test -n "$(BOOT)" -a -f "$(BOOT)/mars_security.inc" || { echo "spike: no 32X boot code (run inside scripts/mars)" >&2; exit 1; }
	$(MD)as -m68000 -I$(BOOT) $< -o $@

$(OUT)/md.elf: $(MDOBJS) md/md.ld
	$(MD)gcc -m68000 -nostdlib -Wl,--build-id=none -Wl,--no-warn-execstack -Wl,--no-warn-rwx-segments -T md/md.ld -Wl,--gc-sections \
		-Wl,-Map=$(OUT)/md.map -o $@ $(MDOBJS) -lgcc

$(OUT)/md.bin: $(OUT)/md.elf
	$(MD)objcopy -O binary $< $@

# ---------------------------------------------------------------- SH-2 program + header -> ROM
$(OUT)/sh/main.o: sh/main.c shared.h Makefile | $(OUT)/sh
	$(SH)gcc $(SHFLAGS) -c $< -o $@

$(OUT)/sh/start.o: sh/start.s $(OUT)/md.bin | $(OUT)/sh
	$(SH)as --big -I$(OUT) $< -o $@

$(OUT)/spike.elf: $(SHOBJS) sh/sh.ld
	$(SH)gcc -m2 -mb -nostdlib -Wl,--build-id=none -Wl,--no-warn-execstack -T sh/sh.ld -Wl,--gc-sections \
		-Wl,-Map=$(OUT)/spike.map -o $@ $(SHOBJS) -lgcc

$(ROM): $(OUT)/spike.elf
	$(SH)objcopy -O binary $< $@
	@test $$(wc -c < $@) -le $(ROMSIZE) || { echo "spike: ROM over $(ROMSIZE) bytes" >&2; exit 1; }
	truncate -s $(ROMSIZE) $@

# RELAY is baked into md/main.o: rebuild it whenever the value changes.
$(OUT)/relay.stamp: FORCE | $(OUT)
	@echo "$(RELAY)" | cmp -s - $@ 2>/dev/null || echo "$(RELAY)" > $@

$(OUT) $(OUT)/md $(OUT)/sh:
	mkdir -p $@

# ---------------------------------------------------------------- checks
# 68000 code only (no 68020+ opcodes), and the cartridge layout the 32X boot ROMs expect.
check: $(ROM)
	@if $(MD)objdump -d $(OUT)/md.elf | grep -Eq '\b(bfffo|bfext[su]|divu\.l|divs\.l|mulu\.l|muls\.l|extb\.l|rtd|cas|chk2|cmp2)\b'; then \
		echo "spike: 68020+ instruction in the 68000 program" >&2; exit 1; fi
	./romcheck $(ROM)

clean:
	rm -rf $(OUT)

.PHONY: all check clean FORCE
