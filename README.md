# retroplat

The platform seam for vintage-targeting C programs, and one backend per
machine. Extracted from RetroWP, a portable word processor for vintage
machines, so that more than one
program can target an Atari ST, an Amiga, a Mac Classic and DOS without
writing the same five backends again.

    include/platform.h   the seam: memory, file I/O, clipboard, timer, font
                         metrics, text and rect drawing, images, windows,
                         dialogs, a byte transport, native<->UTF-8 conversion
    include/types.h      fixed-width types, status enum
    include/endian.h     explicit big-endian read/write helpers
    include/utf8.h       encode/decode
    src/                 the portable half: endian.c, utf8.c
    backends/host/       stub + POSIX, for building and testing off-target
    backends/atari/      GEM / VDI / TOS
    backends/amiga/      AmigaOS
    backends/mac/        Mac Classic Toolbox
    backends/dos/        DOS text mode (Open Watcom)

~6,600 lines. C89, big-endian-canonical on disk and wire, no dynamic
dependencies beyond an allocator wrapper the backend itself provides.

## What belongs here, and what does not

The line is: **"how do I draw a rectangle on this machine" belongs here;
"what rectangles does my program want" does not.**

RetroWP's `platform/atari/app_shell.c` is 4,678 lines of word processor
and stayed behind, along with its action registry, keymap and help
viewer. This directory has no application in it at all, which is what
makes it shareable.

## Checking it

    make check          seam + host backend, native
    make check-atari    m68k-atari-mint-gcc
    make check-amiga    m68k-amigaos-gcc
    make check-mac      Retro68's m68k-apple-macos-gcc
    make check-dos      Open Watcom wcc
    make check-all      every toolchain that is installed

There is nothing to link — a platform layer has no `main()`. The only
thing worth asserting standalone is that it **compiles against nothing
but its own headers**, which is precisely the property that makes it
shareable, and precisely the one a consuming application's build cannot
test: inside that build, the application's headers are already on the
include path.

This is not hypothetical. Four `metrics_*.c` files included RetroWP's
`docmodel.h` to read four text-style bits, invisibly, for as long as the
code existed. Those bits are now `PLAT_STYLE_*` in `platform.h`, next to
the `plat_font_open()` argument that consumes them, and `make check-all`
is what keeps the next one from lasting as long.

An absent cross-toolchain skips rather than fails.

## Consuming it

Point the application's build at `include/` and one `backends/<target>/`,
and compile the backend's `.c` files alongside your own. RetroWP does it
with a `RETROPLAT ?= ../retroplat` knob:

    make RETROPLAT=/path/to/retroplat

Header *filenames* are the same ones RetroWP used before the move
(`types.h`, not `retroplat_types.h`), so an existing consumer's
`#include "types.h"` keeps resolving — to this copy, once the old file is
gone and this directory is on the path.

Two things a consumer should know:

- **`plat_gc` is opaque, and a windowed application needs more.** Each
  backend ships a `metrics_<target>_internal.h` exposing its own graphics
  context (on Atari, `atari_gc { VdiHdl vdi; short window_handle; }`)
  because the application owns its window. That is a documented
  per-backend escape hatch, not a leak.
- **`types.h` may collide.** If your project already defines its own
  `u8`/`u16`/`u32`, a strict C89 compiler is entitled to reject the
  duplicate typedefs when one file includes both. Have yours include this
  one rather than restate it.

## Not here yet

An **Apple IIGS** backend (65816 / GS/OS System 6.x) is the obvious next
one and is not written. It would be the first little-endian *and* first
non-68k target in the set, so it needs the byte-swap-on-I/O treatment
`backends/dos/` already has. `make check-iigs` would gate it from the
first file.

## License

MIT — see [LICENSE](LICENSE).

## Naming

The symbols still read `wp_status` / `WP_OK`, from RetroWP. Renaming them
to `plat_`/`PLAT_` is mechanical and touches every file in every
consumer, so it is deliberately deferred rather than mixed into the move
— new additions to the seam use `PLAT_` from the start, as
`PLAT_STYLE_*` already does.
