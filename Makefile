# Host-side entry points. All compiling happens inside the build image (scripts/md).
MD      := scripts/md
SGDKMK  := make -f /sgdk/makefile.gen

.PHONY: all release debug clean image flash

all: release

release:
	$(MD) $(SGDKMK) release

debug:
	$(MD) $(SGDKMK) debug

clean:
	$(MD) $(SGDKMK) clean

image:
	$(MD) --image

# FlashKit MD (github.com/jfryman/flashkit-md-dotnet >= v1.10.5) on the host.
flash: release
	flashkit-md write-rom out/rom.bin
