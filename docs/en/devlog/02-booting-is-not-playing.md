# Sonic on the Nokia 9210i, Part 2: Booting Is Not the Same as Being Playable

> [Русская версия](../../ru/devlog/02-booting-is-not-playing.md)
>
> [← Previous part](01-hardcore-vibe-coding.md) · [Series index](README.md) · [Next part →](03-framebuffer-war.md)

In Part 1, Sonic the Hedgehog ran on a real Nokia 9210i for the first time. The word “ran” needed a qualification: each frame took about ten seconds.

At first, it seemed that removing diagnostic logging and speeding up a few hot paths would be enough. Those changes did improve performance by roughly an order of magnitude. The result was still a slide show.

The problem was no longer a single bad function. We had hit the limits of the entire port architecture.

## What we had already tried

The obvious optimisations went into the original E32SMS build:

- logging was removed from the inner Z80 loop and scanline renderer;
- frequently used pointers were cached in local variables;
- frame skipping was introduced;
- cached state was added to Z80 opcode handlers;
- the project was switched to a release ARM build.

Every step helped, but the combined result did not change the basic problem. The emulator still spent too much time reaching its own state and presenting each frame.

Instead of continuing to optimise blindly, I looked for a port that had already worked on similar hardware.

## A reference point: SNES9x on the Sony Ericsson P800

I found the source of an **SNES9x port for the Sony Ericsson P800**: UIQ, EKA1 and ARM9 — a platform reasonably close to the Nokia 9210i.

It was an especially useful reference. The Super Nintendo is considerably more complex than the Sega Master System. If SNES9x could be made to run on an early Symbian smartphone, its architecture probably contained answers missing from E32SMS.

The port was associated with **notaz**, while its Symbian shell built on work by **Peter van Sebille**. The tree contained a working `.mmp`, a separate Makefile and an ARM assembly blitter.

The most important discovery, however, was not in the assembly.

## The restriction applies to DLLs, not every EKA1 program

In the original E32SMS, the emulator core lived inside the `.app`. On EKA1, that file is loaded as a DLL. This is why writable global and static data triggered `KERN-EXEC 3` on the device.

To work around it, the entire SMS Plus state had been hidden inside one large heap object and accessed through layers of accessors. The solution worked, but it was expensive for an emulator touching its state thousands of times per frame.

SNES9x showed the opposite picture: `globals.cpp` was full of ordinary writable globals. There was no obvious build-setting magic.

The explanation was simple:

- the user interface was a normal `.app`;
- the emulator core ran in a separate `snes9xemu.exe`;
- the `.app` launched the EXE as an independent process.

An EXE on EKA1 has its own writable data segment. The static-data restriction that breaks DLLs does not apply to it in the same way.

> An EKA1 `.app` is loaded as a DLL, making writable static data problematic.  
> A separate EXE owns a process and can use ordinary global data.

## I did not trust it until the phone proved it

Someone else’s source could have worked for any number of unrelated reasons, so I built a minimal test of my own.

The test EXE performed, in sequence:

- modification of an initialised global variable;
- several repeated writes;
- filling a global array;
- writing into `.bss`;
- changing a global function pointer;
- calling a function through that pointer;
- writing a marker for each stage to a log.

A bare EXE could not be launched by tapping it in the file manager. In that case, the framework tried to load it as a normal application and failed before `E32Main`. I therefore created a tiny `.app` launcher that called:

```cpp
RProcess::Create(...)
```

On the Nokia 9210i, the test reached `PASS`. Every category of static data survived being modified.

The experiment also showed that the standard `abld` EXE build in SDK9210 worked correctly. Manual linking based on the reference project was unnecessary. The earlier EXE had failed because it was launched incorrectly, not because it had been built incorrectly.

## A few traps in the old SDK

The experiment also exposed some characteristic toolchain quirks:

- the `.pkg` file required the correct text encoding;
- wrong line endings could produce an almost empty SIS of roughly 122 bytes without a useful error;
- without an AIF file, the launcher did not appear in the menu;
- validation had to be done with an ARMI UREL build because WINS concealed the memory-model differences.

None of this was related to performance, but any one of these details could waste several hours and distract from the real problem.

## The second discovery: frames were not passed between processes

My first assumption was that the `.app` and `.exe` exchanged completed frames through shared memory. It sounded logical, but also suspicious: copying tens of thousands of pixels between processes every frame would be expensive.

The SNES9x source did something else.

The core obtained the screen framebuffer address through `UserSvr::ScreenInfo`, and its blitter wrote the image directly there. Only infrequent control messages passed between the `.app` and `.exe`: start, pause, exit and ROM selection. The actual frames never travelled through the UI process.

The window existed for focus, keyboard input and cooperation with the window server. Drawing happened directly.

At that point it became clear why the old E32SMS architecture was doomed to remain a slide show:

1. the emulator state sat behind an expensive chain of indirections;
2. each frame followed a slow standard display path;
3. both penalties were built into the chosen `.app` architecture.

## The new plan

The solution was to split the program into two binaries:

- a **thin `.app` launcher** providing the icon and user interface;
- **`e32smsemu.exe`**, containing the SMS Plus core, ordinary global data and direct screen output.

Before moving the entire emulator core, I needed to prove the second foundation: could a separate EXE safely write directly to the Nokia 9210i video memory?

The next part is the story of four failed attempts to display a few ordinary colour bars. How hard could that be?

---

**Hardware:** Nokia 9210i · Symbian OS 6.0 · EKA1 · ARM920T ~52 MHz  
**Architectural reference:** SNES9x/UIQ port by notaz, with a Symbian shell based on work by Peter van Sebille.
