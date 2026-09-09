# tools — toolchain plumbing

Not part of the platform seam. This is the SDK layer: the pieces needed to
get a linked binary onto a machine, which differ per target and belong
with the backends rather than with any one application.

## raw2omf.py — Apple IIGS

Wraps a flat binary in an Apple IIGS **OMF v2** segment, which is what
GS/OS loads.

It exists because of a single gap. [Calypsi](https://github.com/hth313/Calypsi-tool-chains)
is a real ISO C compiler for the 65816 — it takes a full C codebase
without complaint — but its linker emits only S-record/S19/S28/S37/
intel-hex/pgz/prg/raw. None of those is loadable by GS/OS. That one
missing output format is the whole reason the IIGS is not currently a
buildable target.

    raw2omf.py in.raw -o out.omf --org 0x2000 --segname MAIN
    raw2omf.py out.omf --dump          # read it back
    raw2omf.py --selftest

Verified end to end against real Calypsi output: C source → `cc65816` →
`ln65816 --output-format raw` → `raw2omf.py` → an OMF whose payload is
byte-identical to the linker's own binary.

**Scope, stated rather than implied:**

- One fully-linked **absolute** segment, `LCONST` + `END`, no relocation
  records. Correct exactly when the code is already linked to `--org` and
  makes no cross-segment references. Relocatable or multi-segment builds
  need `RELOC`/`INTERSEG` and a real linker pass; this is not that.
- ProDOS metadata is not the file's business: whatever writes the OMF to
  a disk image must also set filetype **$B3 (S16)**, auxtype **$0000**.
- **Not verified on hardware or in an emulator.** No working IIGS romset
  is available here (all four MAME sets verify bad), so the chain has
  never been run under GS/OS. `--selftest` checks the bytes against an
  independent re-parser — deliberately not sharing code with the writer,
  so a mistake cannot hide behind itself — and confirms malformed files
  are rejected. Read a passing selftest as "well-formed", never as
  "works".

## iigs/iigs-pipeline-test.scm

Calypsi linker rules used to prove the pipeline above. **Not a usable
IIGS memory map**: it puts everything in bank 0 so the default 16-bit
code model links. A real map wants the program in its own bank with a
bank-aware code/data model, which is the next piece of SDK work — the
first attempt at `$02/0000` failed exactly there, with `value 131645 is
out of range, allowed range is -32768 to 65535`.
