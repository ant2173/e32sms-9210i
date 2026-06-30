# Sonic on the Nokia 9210i: The E32SMS Development Journal

> [Читать по-русски](../../ru/devlog/README.md)

A five-part series about porting a Sega Master System emulator to the Nokia 9210i, a 2002 Communicator running Symbian OS 6.0, EKA1 and an ARM920T at roughly 52 MHz.

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

## Current status

Parts 1–5 cover the path from the first hardware output at roughly 0.1 FPS to a stable, profiled build at 15.2 FPS. The next instalment will focus on an ARM assembly renderer, frame pacing and keyboard input.

## Development transparency

Large language models provide substantial help with code analysis, patches and documentation. The project author remains responsible for defining the tasks, choosing technical directions, building the software, testing it on original hardware, validating the results and accepting or rejecting each change.
