# Optional DrZ80 (ARM assembly Z80 core)

E32SMS can optionally use **DrZ80** — a Z80 CPU emulator written entirely in
ARM assembly by Reesy & FluBBa — instead of the built-in C interpreter (z80.c).
On the Nokia 9210i (ARM920T) this roughly halves Z80 time (~2.1x measured), a
large speedup because the Z80 runs every frame (frameskip never skips it).

## Why it is not included in this repository

The DrZ80 revision that builds and runs correctly on the SDK9210 toolchain is
the one currently shipped in PicoDrive, whose header reads "free for
non-commercial use; for commercial use, separate licensing terms must be
obtained." That non-commercial restriction is incompatible with this project's
GPL licensing, so **the DrZ80 source is not distributed here**.

(For the record: the original DrZ80 Version 1.0 by Reesy, 2005, is pure GPLv2
and would be license-compatible, but it does not run in our EKA1/ARM920T setup —
it compiles but faults inside DrZ80Run, an unresolved core-level incompatibility.
So in practice the working core is the non-commercial revision, hence the
optional treatment below.)

This mirrors how PicoDrive itself handles license-encumbered components (e.g.
the Helix MP3 decoder): the code is not shipped, but the build can use it if you
obtain it yourself.

The build works out of the box **without** DrZ80, using the GPL C interpreter
(z80.c). DrZ80 is a purely optional speed option.

## How to enable DrZ80

1. Obtain `drz80.S` and `drz80.h` from PicoDrive:
   https://github.com/notaz/picodrive  (cpu/DrZ80/)

2. Prepare `drz80.S` for the SDK9210 toolchain (our 2001-era GNU as differs from
   modern as — see BUILDING.md for full rationale):
   - Expand the PIC_* macros inline (non-PIC forms) and drop the
     `#include <pico/arm_features.h>` line: `PIC_BT(x)`->`.word x`;
     `PIC_XB(,r,s)`->`ldr pc, [pc, r, s]`; `PIC_LDR(r,t,a)`->`ldr r, =a`;
     remove `PIC_LDR_INIT()`. (Our toolchain does not honor C-preprocessor
     `#define` inside `.S`.)
   - Remove apostrophes/quotes from comments (the C preprocessor chokes on them).
   - Comment out the empty `.pool` whose only `ldr =DAATable` is commented out
     (old `as` errors on an empty literal pool).

3. Place `drz80.S` and `drz80.h` in `smsplus/cpu/`.

4. In `e32smsemu.mmp` add:
       SOURCE          cpu\drz80.S
       SOURCE          cpu\drz80_glue.c
       MACRO           USE_DRZ80

5. Rebuild. `drz80_glue.c` (the adapter connecting DrZ80 to SMS Plus — part of
   THIS project, GPL) exposes the same z80_* entry points, so nothing else
   changes. Without `USE_DRZ80`, z80.c (the C interpreter) is used instead.

## Credit

DrZ80 is by **Reesy & FluBBa**. It is used here only as an optional, user-
supplied component; this project claims no rights over it and does not
redistribute it. Integration approach modeled on PicoDrive's pico/z80if.c
(notaz / irixxxx).
