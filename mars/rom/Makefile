# The 32X ROM (lode/mars/summary.md): a 68000 I/O program (md/) inside an SH-2 image (sh/), built in the 32X
# image (scripts/mars). From the repo root: `make mars` (cart: relay feed) or `make mars-sim` (emulator: demo feed).
#   make FAKE=1      # emulator build
MD      = m68k-elf-
SH      = sh-elf-
TOP     = ../..
SRC     = $(TOP)/src
MW      ?= $(MARS_MW)
BOOT    ?= $(MARS_BOOT)
FAKE    ?=
VARIANT = $(if $(FAKE),emu,cart)
OUT     ?= $(TOP)/out/mars/$(VARIANT)
ROM     = $(OUT)/mwradar.32x
ROMSIZE = 1048576

DEFS    = $(if $(FAKE),-DFAKE_FEED) -DMARS $(EXTRA)
COMMON  = -Wall -Wextra -Wno-unused-parameter -ffreestanding -fno-builtin -fomit-frame-pointer \
          -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-strict-aliasing $(DEFS)

# ---------------------------------------------------------------- 68000: I/O processor -> md.bin
MDFLAGS = -m68000 -Os $(COMMON) -Imd/compat -Icompat -I. -I$(MW)/inc -I$(SRC)
MDSRC   = md/main.c md/link.c md/vdp.c md/task.c md/uart_ram.c md/16c550.c compat/libc.c \
          $(SRC)/feed.c $(SRC)/devcfg.c $(MW)/src/comm.c $(MW)/src/lsd.c $(MW)/src/megawifi.c
MDSRC  += $(if $(FAKE),$(SRC)/feed_fake.c,$(SRC)/feed_relay.c $(SRC)/net.c $(SRC)/fmt.c)
MDOBJS  = $(OUT)/md/boot.o $(OUT)/md/fakedata.o $(addprefix $(OUT)/md/,$(notdir $(MDSRC:.c=.o)))


all: $(ROM) check

MDDEPS  = $(wildcard md/*.h md/compat/*.h md/compat/ext/mw/*.h compat/*.h *.h) Makefile
MDCC    = $(MD)gcc $(MDFLAGS) $(if $(filter net.o,$(notdir $@)),-Dmw_init=md_mw_init) -c $< -o $@
$(OUT)/md/%.o: md/%.c $(MDDEPS) | $(OUT)/md ; $(MDCC)
$(OUT)/md/%.o: compat/%.c $(MDDEPS) | $(OUT)/md ; $(MDCC)
$(OUT)/md/%.o: $(SRC)/%.c $(MDDEPS) | $(OUT)/md ; $(MDCC)
$(OUT)/md/%.o: $(MW)/src/%.c $(MDDEPS) | $(OUT)/md ; $(MDCC)

$(OUT)/md/boot.o: md/boot.s | $(OUT)/md
	@test -f "$(BOOT)/mars_security.inc" || { echo "mars: no 32X boot code (run inside scripts/mars)" >&2; exit 1; }
	$(MD)as -m68000 -I$(BOOT) $< -o $@

$(OUT)/md/fakedata.o: md/fakedata.s $(wildcard $(TOP)/res/fake/*.bin) | $(OUT)/md
	$(MD)as -m68000 -I$(TOP) $< -o $@

$(OUT)/md.elf: $(MDOBJS) md/md.ld
	$(MD)gcc -m68000 -nostdlib -Wl,--build-id=none -Wl,--no-warn-execstack -Wl,--no-warn-rwx-segments \
		-T md/md.ld -Wl,--gc-sections -Wl,-Map=$(OUT)/md.map -o $@ $(MDOBJS) -lgcc

$(OUT)/md.bin: $(OUT)/md.elf
	$(MD)objcopy -O binary $< $@

# ---------------------------------------------------------------- SH-2s: the app + renderer -> the ROM
# sh.shared is volatile so spin-waits re-read it; handing its arrays to string and draw helpers is fine (the
# memory is cache-through anyway), so the discarded-qualifier warnings are off here.
SHFLAGS = -m2 -mb -O2 $(COMMON) -Wno-discarded-qualifiers -Wno-discarded-array-qualifiers \
          -Ish/compat -Icompat -I. -Ish -I$(SRC)
SHAPP   = main.c traffic.c tlist.c popup.c setup.c keyboard.c settings.c geo.c fmt.c airports.c theme.c feed.c devcfg.c
SHSRC   = $(wildcard sh/*.c) compat/libc.c $(addprefix $(SRC)/,$(SHAPP))
SHOBJS  = $(OUT)/sh/start.o $(addprefix $(OUT)/sh/,$(notdir $(SHSRC:.c=.o)))

SHDEPS  = $(wildcard sh/*.h sh/compat/*.h sh/compat/ext/mw/*.h compat/*.h *.h) Makefile
SHCC    = $(SH)gcc $(SHFLAGS) -c $< -o $@
$(OUT)/sh/%.o: sh/%.c $(SHDEPS) | $(OUT)/sh ; $(SHCC)
$(OUT)/sh/%.o: compat/%.c $(SHDEPS) | $(OUT)/sh ; $(SHCC)
$(OUT)/sh/%.o: $(SRC)/%.c $(SHDEPS) | $(OUT)/sh ; $(SHCC)

$(OUT)/sh/start.o: sh/start.s $(OUT)/md.bin | $(OUT)/sh
	$(SH)as --big -I$(OUT) $< -o $@

$(OUT)/mwradar.elf: $(SHOBJS) sh/sh.ld
	$(SH)gcc -m2 -mb -nostdlib -Wl,--build-id=none -Wl,--no-warn-execstack -Wl,--no-warn-rwx-segments \
		-T sh/sh.ld -Wl,--gc-sections -Wl,-Map=$(OUT)/mwradar.map -o $@ $(SHOBJS) -lgcc

$(ROM): $(OUT)/mwradar.elf
	$(SH)objcopy -O binary $< $@
	@test $$(wc -c < $@) -le $(ROMSIZE) || { echo "mars: ROM over $(ROMSIZE) bytes" >&2; exit 1; }
	truncate -s $(ROMSIZE) $@

check: $(ROM)
	@if $(MD)objdump -d $(OUT)/md.elf | grep -Eq '\b(bfffo|bfext[su]|divu\.l|divs\.l|mulu\.l|muls\.l|extb\.l|rtd|cas|chk2|cmp2)\b'; then \
		echo "mars: 68020+ instruction in the 68000 program" >&2; exit 1; fi
	$(TOP)/mars/spike/romcheck $(ROM)
	@$(SH)size -A $(OUT)/mwradar.elf | awk '/^\.data|^\.bss|^\.shared/ {print "mars:", $$1, $$2, "bytes"}'

$(OUT) $(OUT)/md $(OUT)/sh:
	mkdir -p $@

# devcfg.c includes the generated src/version.h and the optional src/dev_config.h, which the rules above
# can't see appear: rebuild it every time, as the Genesis build does.
$(OUT)/md/devcfg.o $(OUT)/sh/devcfg.o: FORCE

clean:
	rm -rf $(OUT)

.PHONY: all clean check FORCE
