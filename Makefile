# retroplat -- the portable platform seam, and one backend per vintage
# machine. Extracted from RetroWP (~/dev/focused) per its
# docs/platform-extraction.md.
#
# There is nothing to link here. A platform layer has no main(), so the
# only thing worth asserting about it standalone is that it COMPILES
# against nothing but its own headers -- which is exactly the property
# that makes it shareable. An application dependency creeping back in is
# invisible inside a consuming app's build, where that app's headers are
# already on the include path; it is a hard error here.
#
#   make check          seam + host backend, native
#   make check-atari    m68k-atari-mint-gcc
#   make check-amiga    m68k-amigaos-gcc
#   make check-mac      Retro68's m68k-apple-macos-gcc
#   make check-dos      Open Watcom wcc
#   make check-all      every toolchain that is installed
#
# Always recompiles: this is a gate, and a gate that can be stale is not
# a gate. It is a couple of seconds over ~6,600 lines.

BUILD = build

CC      ?= cc
ATARICC ?= m68k-atari-mint-gcc

AMIGA_TOOLROOT ?= $(HOME)/opt/amiga
AMIGACC        ?= $(AMIGA_TOOLROOT)/bin/m68k-amigaos-gcc
AMIGA_CPU      ?= -m68020

MAC_TOOLROOT ?= $(HOME)/opt/Retro68-build/toolchain
MACCC        ?= $(MAC_TOOLROOT)/bin/m68k-apple-macos-gcc

DOS_TOOLROOT ?= $(HOME)/opt/watcom
DOSCC        ?= $(DOS_TOOLROOT)/binl64/wcc
DOS_ENV       = WATCOM=$(DOS_TOOLROOT) PATH=$(DOS_TOOLROOT)/binl64:$$PATH

# Relaxed rather than strict, and deliberately so: the native GEM, MUI,
# Toolbox and BIOS headers these backends include are not -pedantic-clean,
# and that is not this gate's business. The gate is the include path.
CFLAGS_HOST  = -Wall -Wextra -O2
CFLAGS_ATARI = -Wall -Wextra -O2
CFLAGS_AMIGA = -Wall -Wextra $(AMIGA_CPU)
CFLAGS_MAC   = -Wall -Wextra -O2
CFLAGS_DOS   = -0 -ml -bt=dos -wx

# The portable half: no platform calls, no application, nothing to stub.
PORTABLE_SRC = src/endian.c src/utf8.c

.PHONY: check check-atari check-amiga check-mac check-dos check-gtk check-all clean

check:
	@mkdir -p $(BUILD)/host
	@for f in $(PORTABLE_SRC) backends/host/*.c; do \
		$(CC) $(CFLAGS_HOST) -Iinclude -Ibackends/host \
			-c $$f -o $(BUILD)/host/`basename $$f .c`.o || exit 1; \
	done
	@echo "check: seam + host backend"

check-atari:
	@mkdir -p $(BUILD)/atari
	@for f in $(PORTABLE_SRC) backends/atari/*.c; do \
		$(ATARICC) $(CFLAGS_ATARI) -Iinclude -Ibackends/atari \
			-c $$f -o $(BUILD)/atari/`basename $$f .c`.o || exit 1; \
	done
	@echo "check-atari: GEM / VDI / TOS backend"

check-amiga:
	@mkdir -p $(BUILD)/amiga
	@for f in $(PORTABLE_SRC) backends/amiga/*.c; do \
		$(AMIGACC) $(CFLAGS_AMIGA) -Iinclude -Ibackends/amiga \
			-c $$f -o $(BUILD)/amiga/`basename $$f .c`.o || exit 1; \
	done
	@echo "check-amiga: AmigaOS backend"

# The GTK backend: Cairo for rects, Pango for text. The first backend in
# this library for a machine that is still made -- and the only one whose
# twips conversion is exact for the same reason the Mac's is, since Pango
# counts in points and a twip is 1/20 of one.
check-gtk:
	@mkdir -p $(BUILD)/gtk
	@for f in $(PORTABLE_SRC) backends/gtk/*.c; do \
		$(CC) $(CFLAGS_HOST) `pkg-config --cflags gtk+-3.0` \
			-Iinclude -Ibackends/gtk -Ibackends/host \
			-c $$f -o $(BUILD)/gtk/`basename $$f .c`.o || exit 1; \
	done
	@echo "check-gtk: GTK3 + Cairo + Pango backend"

check-mac:
	@mkdir -p $(BUILD)/mac
	@for f in $(PORTABLE_SRC) backends/mac/*.c; do \
		$(MACCC) $(CFLAGS_MAC) -Iinclude -Ibackends/mac \
			-c $$f -o $(BUILD)/mac/`basename $$f .c`.o || exit 1; \
	done
	@echo "check-mac: Mac Classic Toolbox backend"

check-dos:
	@mkdir -p $(BUILD)/dos
	@for f in $(PORTABLE_SRC) backends/dos/*.c; do \
		env $(DOS_ENV) $(DOSCC) $(CFLAGS_DOS) -i=include -i=backends/dos \
			-i=$(DOS_TOOLROOT)/h \
			-fo=$(BUILD)/dos/`basename $$f .c`.o $$f || exit 1; \
	done
	@echo "check-dos: DOS text-mode backend"

# An absent cross-toolchain is not a failed gate.
check-all: check
	@command -v $(ATARICC) >/dev/null 2>&1 && $(MAKE) check-atari || echo "-- no Atari toolchain, skipped"
	@test -x $(AMIGACC) && $(MAKE) check-amiga || echo "-- no Amiga toolchain, skipped"
	@pkg-config --exists gtk+-3.0 && $(MAKE) check-gtk || echo "-- no GTK3, skipped"
	@test -x $(MACCC) && $(MAKE) check-mac || echo "-- no Mac toolchain, skipped"
	@test -x $(DOSCC) && $(MAKE) check-dos || echo "-- no DOS toolchain, skipped"

clean:
	rm -rf $(BUILD)
