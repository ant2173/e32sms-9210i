# Engineering influences and prior art

The following projects informed the engineering approach used by E32SMS.
They are listed here for technical provenance and credit. No source code from
these projects is intended to be included unless a specific file is identified
in `THIRD_PARTY_NOTICES.md`.

## SDL EPOC / Series 80 video backend

Studied for the Nokia 9210/9210i screen-memory layout, palette offset and the
need to gate direct framebuffer writes by window-server focus.

## SNES9x UIQ port by notaz

Studied for the two-binary architecture: a thin Symbian application launcher
starting a standalone emulator process through `RProcess::Create`.

## CDoom for Nokia 9210

Used as corroborating prior art showing that the screen-capture/output path on
Series 80 was implemented through the SDL/EPOC layer.

## Attribution boundary

These entries describe ideas, architecture and research leads. If later audit
finds copied code, tables, comments or other protectable expression, that
material must be moved to `THIRD_PARTY_NOTICES.md` with its exact license and
copyright notice.
