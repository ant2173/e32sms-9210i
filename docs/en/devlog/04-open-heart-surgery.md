# Sonic on the Nokia 9210i, Part 4: Open-Heart Surgery

> [Русская версия](../../ru/devlog/04-open-heart-surgery.md)
>
> [← Previous part](03-framebuffer-war.md) · [Series index](README.md) · [Next part →](05-chasing-frames.md)

By the start of this part, every key element of the new architecture had been tested independently:

- an application could launch a separate EXE;
- the EXE could use ordinary global data;
- direct framebuffer output worked on a real Nokia 9210i;
- checking focus prevented the conflict with the window server.

Now those experiments had to become a complete emulator. The old E32SMS needed to be cut apart, the SMS Plus core moved into a separate process and the proven video path connected to it.

## The two anchors holding back the old build

The original port grew inside the Symbian shell of the Frodo emulator. That made it possible to obtain a window and event loop quickly, but it gradually created two systemic problems.

### State on the heap

Because an EKA1 `.app` was loaded as a DLL, the SMS Plus state had been moved from global structures into one large heap object. Every access to registers, memory or tables passed through an accessor.

For an ordinary application, the overhead might have been invisible. For an emulator performing enormous numbers of such accesses per frame, it accumulated.

### A slow display path

The completed frame travelled through the output path inherited from the shell instead of being written directly to the framebuffer.

Both problems could be solved by one architectural change: the core would run in a separate `e32smsemu.exe`, while the application carrying the icon would do little more than launch it.

## Frodo was not what I thought it was

Before beginning the migration, I checked which files actually participated in the build. It turned out that the Commodore 64 engine itself had not been compiled for a long time. Most of the Frodo source was simply lying beside the project as dead weight.

The binary contained not a C64 emulator, but remnants of its Symbian framework:

- a wrapper object;
- settings;
- timers;
- numerous ties to the user interface.

The target for removal was therefore not a “heavy C64 core,” but the architecture left around it.

## Returning to ordinary globals

At this point, an earlier workaround unexpectedly paid off.

SMS Plus had previously been adapted to heap-based state through macros. The core still referred to expressions such as `sms.console`, while the preprocessor silently redirected them to fields inside the large state object.

For the EXE, I only needed to replace that macro layer with real global definitions. Most of the core did not have to be converted back by hand.

The expensive chain of indirections disappeared at one central point.

## The new EXE

The separate project included:

- the SMS Plus C source files;
- a new `E32Main`;
- ROM loading;
- CPU, VDP and renderer initialisation;
- the main emulation loop;
- the tested window-and-focus scaffold;
- direct framebuffer output with the 32-byte offset.

The first build stopped on incorrect paths to shared headers. That was fixed quickly.

The second build produced hundreds of `parse error` messages in the Z80 code.

## How an empty macro broke ancient C

The `gcc 2.9-psion` compiler effectively enforced C89 rules: local variable declarations had to appear at the beginning of a block, before executable statements.

In the old code, a state-caching macro appeared before those declarations. After the switch back to direct globals, it was no longer needed, so I made it empty.

But an empty expansion inside thousands of opcode handlers became a standalone empty statement. The variable declarations that followed were therefore no longer at the beginning of the block, making them illegal under C89.

Instead of being removed entirely, the macro was changed to expand into a harmless dummy declaration. The Z80 source compiled again.

The third attempt completed without errors, although the old code still produced roughly fifteen hundred warnings, mostly from the temporary dummy and the disabled sound path.

## The first run of the new architecture

The thin `.app` launcher started `e32smsemu.exe` as a separate process. The EXE loaded the ROM and began executing frames.

The Sonic the Hedgehog title screen appeared again. This time, however, Sonic no longer moved once every several seconds. The animation looked alive: the colours were correct, the picture was stable and it was centred on the display.

<!-- Add video: title screen running under the new EXE architecture -->

Direct access to globals and direct framebuffer output had removed the two largest architectural penalties.

## Why the program closed after one minute

The new build ran reliably for about sixty seconds and then exited by itself.

The suspected system bug turned out not to be a system bug at all. The test loop still contained a limit of 600 frames, a safety measure added in case the program locked up. At the current speed, those 600 frames took roughly one minute.

Once the limit was removed, the loop continued running.

The incident also provided a rough first measurement: approximately **10 frames per second**. That was still far from full speed, but it was no longer the slide show of the first version.

## What had fundamentally changed

The old design:

```text
.app + Frodo shell
        ↓
heap state + accessors
        ↓
slow display path
```

The new design:

```text
.app launcher
        ↓ RProcess::Create
 e32smsemu.exe
        ↓
ordinary globals + direct framebuffer
```

For the first time, further performance work could target specific stages of the frame instead of trying to compensate for the wrong architecture with increasingly complicated workarounds.

The next task was to stop judging speed by eye. I needed an exact FPS measurement and a breakdown of the frame: Z80, video processing, palette conversion and screen output.

In the next part, intuition points to the wrong section of code twice.

---

**Stage status:** separate `e32smsemu.exe`, ordinary global data, direct focus-gated framebuffer output, roughly 10 FPS by an initial estimate.
