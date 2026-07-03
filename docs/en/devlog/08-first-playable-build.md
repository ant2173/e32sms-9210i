# Sonic on the Nokia 9210i, Part 8: The First Playable Build

**Development log — July 2026**  
[Русская версия](../../ru/devlog/08-first-playable-build.md)  
[← Part 7: The First ARM Assembly Blitter](07-first-arm-assembly.md)

For the first seven parts, Sonic could move — but only by himself.

The emulator booted, drew the game on the physical Nokia framebuffer, survived a major architectural rewrite, gained frameskip and finally acquired its first ARM assembly routine. Yet it was still only a technical demonstration. The built-in attract mode ran; the person holding the device could not actually play.

That changed with keyboard input.

## Do the smallest useful thing first

The tempting approach was to begin with a polished settings screen and user-configurable key mapping. That would also have been the wrong approach.

A remapping interface is a layer above working input. At this point there was no verified input path at all. Building both layers at once would have meant debugging the event system and the user interface simultaneously, with no clear way to tell which one had failed.

So the work was split into two stages:

1. implement a minimal hard-coded control layout;
2. add configurable remapping later, after the basic path is proven.

The first goal was not elegance. It was to make Sonic obey a key press on the real Nokia 9210i.

## Finding the actual key codes

Symbian exposes named key constants, but relying on assumptions about a specific twenty-five-year-old keyboard would have been careless. The physical codes had to be measured on the device.

A diagnostic event reader was added to the game loop. Every press and release was written to the log. I then pressed the keys in a known sequence:

- up;
- down;
- left;
- right;
- two adjacent action keys;
- Enter.

That produced an exact map from physical Nokia keys to the codes delivered by the system. No guessing, no emulator assumptions and no dependence on incomplete documentation.

The event-handling pattern was adapted from the gnuboy port for Series 60, where the same class of problem had already been solved: subscribe to keyboard events, fetch each event and read its key code.

## Connecting the keyboard to the Master System controller

The Sega Master System controller is simple:

- four directions;
- two action buttons;
- Pause.

Inside SMS Plus, those inputs are represented as controller-state bits. A key press sets the relevant bit; a key release clears it.

The first fixed layout uses:

- arrow keys for movement;
- two neighboring keyboard keys for Buttons 1 and 2;
- Enter for Pause.

This is not the final input system. It is deliberately small, direct and easy to verify.

## The first real play session

The build was compiled, installed and launched on the Nokia 9210i.

At the title screen, the first key press registered. Sonic moved right. The jump button worked. Directions were not swapped. Key releases did not leave the character stuck in motion.

Then came the milestone that mattered more than another benchmark:

**the first level was completed by hand.**

Sonic ran through the stage, collected rings, reached the goal and loaded the next level. For the first time, E32SMS was not merely showing a game. It was accepting human input and behaving as an actual emulator.

That is a qualitative change. A moving title screen is a demonstration. A completed level is a playable system.

## Playable does not mean full speed

The current build is playable, but it is not yet close to full real-time speed.

The emulator advances at approximately **27 emulated frames per real second**, against the Master System target of about 60. With `frameskip = 1`, around **13.5 frames per second** are actually drawn to the screen.

That means the whole game runs at roughly 45% of its intended pace. The controls work correctly, but Sonic responds more slowly than he should because the emulated game itself is progressing slowly. This is not merely a display problem.

The current performance map already shows the next targets:

- background rendering;
- sprite rendering;
- additional ARM assembly work;
- possible reductions in per-frame overhead.

Increasing frameskip further remains available as a crude fallback, but it would reduce visual smoothness without fixing the underlying emulation speed.

## What comes next

The immediate input milestone is complete:

- physical key codes were measured on hardware;
- press and release events are handled;
- directions and action buttons work;
- Pause is connected;
- the first level can be completed.

The next input task is a proper remapping layer, ideally with a simple settings interface. Performance work can then continue without the emulator reverting to a passive demonstration.

For now, the important result is simple:

A Nokia communicator released in 2001 can now run Sonic — and the person holding it can actually play.

To be continued.

---

**Current result:** first level completed manually on original hardware.  
**Performance:** ~27.0 emulated fps / ~13.5 rendered fps with `frameskip = 1`.  
**Input:** hard-coded Nokia keyboard mapping for directions, two buttons and Pause.  
**Hardware:** Nokia 9210i, Symbian OS 6.0, EKA1, ARM920T ~52 MHz, 640×200 Color4K display.  
**Build:** Symbian SDK9210, gcc 2.9-psion, ARMI UREL.  
**Core:** SMS Plus by Charles MacDonald.  
**Development method:** AI-assisted source work; builds, tests and conclusions validated on original hardware.  
**Input reference:** event-handling approach adapted from the gnuboy port for Series 60. See the repository's third-party notices for attribution and licensing details.
