# Sonic on the Nokia 9210i: The E32SMS Development Journal

> [Читать по-русски](../../ru/devlog/README.md)

A development series about porting a Sega Master System emulator to the Nokia 9210i, a 2002 Communicator running Symbian OS 6.0, EKA1 and an ARM920T at roughly 52 MHz.

The project is developed with substantial AI assistance, but every hypothesis, build and code change is tested on original hardware. The project author directs the investigation, builds the software, installs SIS packages, collects logs and makes the final call based on hardware results.

## The series

1. [Hardcore Vibe Coding: How I Got Sonic Running on a Nokia 9210i](01-hardcore-vibe-coding.md)  
   The first real frame, `KERN-EXEC 3`, writable static data, COFF object files and an unaligned ARM write.

2. [Booting Is Not the Same as Being Playable](02-booting-is-not-playing.md)  
   Why local optimisations could not save the original architecture, what a rare SNES9x port for UIQ revealed, and why the emulator core needed its own EXE.

3. [Four Lost Rounds with the Framebuffer](03-framebuffer-war.md)  
   Direct framebuffer access, diagonal noise, the window server, a 32-byte palette offset and focus-gated drawing.

4. [Open-Heart Surgery](04-open-heart-surgery.md)  
   Moving SMS Plus into `e32smsemu.exe`, restoring ordinary global data and reaching roughly 10 FPS with the new architecture.

5. [Chasing Frames](05-chasing-frames.md)  
   Profiling, three hardware-tested optimisations and a rise from 12.5 to 15.2 FPS.

6. [From Slideshow to Motion](06-from-slideshow-to-motion.md)  
   Frame pacing, a merged remap-and-blit pass and the jump in perceived smoothness.

7. [The First ARM Assembly](07-first-arm-assembly.md)  
   The first hand-written ARM assembly routine, three toolchain traps and a faster VRAM blitter.

8. [The First Playable Build](08-first-playable-build.md)  
   Keyboard input, discovering the real scancodes on hardware and the first level cleared by hand.

9. [The Last Cheap Win](09-last-cheap-win.md)  
   A per-scanline visible-sprite list, why the biggest chunk was not the best target, and the optimisation ceiling.

10. [A Heart Transplant](10-heart-transplant.md)  
    Replacing the C Z80 interpreter with the DrZ80 ARM-assembly core for a ~2.1× CPU speedup — and the licensing lesson that followed.

## Current status

Parts 1–10 cover the path from the first hardware output at roughly 0.1 FPS to a playable emulator with keyboard input and an ARM-assembly Z80 core. The Z80 core (DrZ80) is an optional, user-supplied component for licensing reasons; the default build uses the GPL C interpreter. See `DRZ80_OPTIONAL.md`.

## Development transparency

Large language models provide substantial help with code analysis, patches and documentation. The project author remains responsible for defining the tasks, choosing technical directions, building the software, testing it on original hardware, validating the results and accepting or rejecting each change.
