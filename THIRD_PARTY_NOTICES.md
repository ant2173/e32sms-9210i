# Third-party notices

This file describes third-party code that is actually present in the current
E32SMS source tree. It does not replace the license notices embedded in the
individual source files.

## SMS Plus emulator core

Portions of this repository are derived from SMS Plus by Charles MacDonald.
The retained SMS Plus source identifies itself as distributed under the GNU
General Public License, version 2 or (at the recipient's option) any later
version. Original copyright and license notices must remain intact.

E32SMS modifies parts of the core for Symbian OS / EKA1, including global-state
handling, rendering, profiling and direct RGB444 framebuffer output. Modified
upstream files should carry a dated modification notice.

## Juergen Buchmueller Z80 emulator

`smsplus/cpu/z80.c` contains the Portable Z80 emulator by Juergen Buchmueller.
The exact notice embedded in that file permits non-commercial use and
redistribution subject to its stated conditions, requires attribution and
modification notices, and requires separate permission for commercial use.

This notice is not the GNU GPL. The exact text is preserved in the source file
and in `LICENSES/Z80-NONCOMMERCIAL-NOTICE.txt`.

**Release status:** until the licensing of this exact Z80 core is clarified or
the core is replaced, E32SMS should not be represented as uniformly licensed
under the GPL, and binary releases should not be published.

## DrZ80 (optional, NOT present in this repository)

E32SMS can optionally use **DrZ80**, a Z80 CPU core written in ARM assembly by
**Reesy & FluBBa**, in place of the C interpreter (`smsplus/cpu/z80.c`). On the
Nokia 9210i this roughly halves Z80 time, a large gain because the Z80 runs on
every frame.

DrZ80 is **not distributed with this repository.** The DrZ80 revision that builds
and runs correctly on the SDK9210 toolchain is offered as "free for
non-commercial use", which is incompatible with this project's GPL licensing.
(The original DrZ80 Version 1.0, 2005, is pure GPLv2 and would be compatible, but
it does not run in the EKA1 / ARM920T environment here — it compiles yet faults
inside the core's own execution, an unresolved incompatibility.) The core is
therefore treated as an optional, user-supplied component, mirroring how
PicoDrive handles its own licence-encumbered components.

`smsplus/cpu/drz80.S`, `smsplus/cpu/drz80.h` and `arm_features.h` are excluded via
`.gitignore` and must be obtained by the user (see `DRZ80_OPTIONAL.md`). The
adapter that connects DrZ80 to SMS Plus — `smsplus/cpu/drz80_glue.c` — is original
work, part of E32SMS, licensed under the GPL like the rest of the project; it
reproduces no DrZ80 source and was modeled on the interface approach of
PicoDrive's `pico/z80if.c`. The default build uses the GPL C interpreter and does
not require DrZ80.

## MAME-derived rendering helpers

`smsplus/render.c` identifies its dword access helpers as originating from
MAME's `drawgfx.c`. The exact upstream version and applicable license still
need to be documented. This item must remain open until provenance is verified
or the helpers are independently rewritten.

## Components not present in the publication tree

The cleaned publication tree does not include the Frodo/C64 engine, C64 ROMs,
disk images, game ROMs, YM2413/EMU2413 sources or SN76489 emulator sources.
They therefore should not be described as bundled components of the current
repository.
