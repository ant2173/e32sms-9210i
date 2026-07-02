# E32SMS licensing status

**Important:** this repository does **not currently have a single unified
license**. Different files are subject to different terms, and one unresolved
license-compatibility issue remains in the current source tree.

This file is a licensing map and status notice. It does not replace the license
notices contained in individual source files and does not grant rights in code
owned by third parties.

## 1. E32SMS-specific code and modifications

To the extent that copyright in the following material is owned by the E32SMS
project maintainer or other E32SMS contributors, that material is offered under
the **GNU General Public License, version 2 or (at your option) any later
version** (`GPL-2.0-or-later`):

- the Symbian standalone EXE entry point and direct-to-VRAM output path;
- the Series 80 launcher and `RProcess::Create` integration;
- the EKA1 writable-state migration and related glue;
- E32SMS build files and packaging files;
- E32SMS-specific changes to upstream source files.

The full GPL text is provided in:

`LICENSES/GPL-2.0.txt`

Licensing an E32SMS modification under the GPL does not remove or replace the
license and copyright notices applicable to the underlying upstream file.

## 2. SMS Plus-derived code

Most files under `smsplus/` derive from **SMS Plus**, copyright Charles
MacDonald, and retain the GPL notice contained in the upstream source files.
Those files remain subject to their original notices and to the GPL version
stated in those notices.

The E32SMS project does not claim ownership of the upstream SMS Plus code.

## 3. Z80 CPU core: separate non-commercial terms

The file:

`smsplus/cpu/z80.c`

contains a separate notice by Juergen Buchmueller. That notice describes the
code as freeware for **non-commercial purposes**, requires credit, requires the
entire notice to remain in the source, and requires modified files to be marked
as changed. Commercial use requires separate permission from the original
author.

The original notice is reproduced in:

`LICENSES/Z80-NONCOMMERCIAL-NOTICE.txt`

No additional rights in this Z80 core are granted by the E32SMS project.
The provenance and licensing of companion files associated with that core,
including `z80.h`, `z80daa.h`, `cpuintrf.h`, and `osd_cpu.h`, should be treated
as unresolved until separately verified.

## 4. Current compatibility problem

The non-commercial restriction in the Z80 notice is an additional restriction
not found in the GPL. Accordingly, the current source tree should **not** be
represented as a single work that is cleanly redistributable under the GPL
alone.

The E32SMS maintainer does not represent or warrant that all present terms can
be satisfied simultaneously when redistributing the combined source tree or a
compiled binary. Publication of this repository does not itself resolve that
conflict and does not create permission that the relevant copyright holders
have not granted.

Until the Z80 core is replaced with a GPL-compatible implementation, or the
necessary permission or relicensing is confirmed, the current tree should be
regarded as a development and license-audit snapshot rather than a legally
cleared public release. In particular, no compiled E32SMS binary should be
distributed on the assumption that the whole program is licensed solely under
the GPL.

## 5. Possible MAME-derived material in `render.c`

`smsplus/render.c` identifies certain 32-bit memory-access macros as originating
from MAME's `drawgfx.c`. The exact upstream version and applicable license have
not yet been verified.

Those lines must therefore remain attributed and should be treated as having
unresolved provenance until they are either:

1. traced to an identified upstream version and licensed accordingly; or
2. independently rewritten and documented as such.

## 6. Frodo and other studied projects

The current minimal E32SMS build does not include or compile the Frodo C64
engine. Frodo is part of the project's development history, not a component to
which this licensing map presently grants or restates rights.

SDL EPOC, SNES9x UIQ, and CDoom were studied as engineering references for such
matters as framebuffer layout, focus gating, and process-launch architecture.
No source code from those projects is intended to be included in the current
minimal tree. They should be credited in `docs/engineering/INFLUENCES.md`, not
listed as code licensed by this repository.

## 7. Documentation, articles, and photographs

Unless a file expressly states otherwise, no separate reuse license is
currently granted for original documentation, development-journal articles, or
photographs in this repository. They remain subject to applicable copyright.
A Creative Commons license may be added later as a separate decision.

## 8. ROMs, games, SDKs, and trademarks

No game ROMs, firmware images, proprietary SDK files, or third-party
installation media are licensed or distributed by this project.

Sega Master System, Game Gear, Sonic the Hedgehog, Nokia, Symbian, and other
names and marks belong to their respective owners. Their mention is solely for
identification and technical documentation.

## 9. No warranty

The software and documentation are provided without warranty. To the extent
that the GPL applies, its warranty disclaimer controls. For all other material,
the E32SMS contributors disclaim warranties to the maximum extent permitted by
law.
