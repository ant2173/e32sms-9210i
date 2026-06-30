# Sonic on the Nokia 9210i, Part 5: Chasing Frames

> [Русская версия](../../ru/devlog/05-chasing-frames.md)
>
> [← Previous part](04-open-heart-surgery.md) · [Series index](README.md)

After the core moved into a separate EXE, the Sonic the Hedgehog title screen looked almost alive. “Almost” is a poor unit of measurement.

Until the exact frame rate and the cost of each stage were known, every optimisation remained guesswork. The next phase therefore began not with assembly, but with a stopwatch.

The central lesson was simple: **do not optimise what merely looks slow. Measure it first.**

## The first counter failed

The initial speed measurement used system ticks. It produced an absurd result of roughly two hundred frames per second. That was clearly impossible for an emulator on a 52 MHz ARM9, especially since an NTSC game itself targets roughly 60 frames per second.

The frequency of that counter had been interpreted incorrectly on this system. It could not be treated as a ready-made time scale.

The reliable measurement used absolute time in microseconds. The old SDK introduced another complication: a 64-bit value was represented by a special class rather than an ordinary built-in type. The code had to handle its parts carefully and avoid modern arithmetic the compiler did not understand.

With that fixed, the baseline became clear: approximately **12.5 FPS**.

## What one frame actually cost

The next step was to place separate timers around the major stages:

- execution of the emulated Z80;
- VDP work and construction of the background and sprites;
- conversion of the indexed image into colour;
- output of the finished frame to the framebuffer;
- all remaining housekeeping.

Intuition made the Z80 the prime suspect. In an emulator, the CPU core is the first component one is tempted to optimise.

The profile showed the opposite:

- Z80 execution took about 18 ms;
- total rendering took about 42 ms;
- the rest of the frame was considerably smaller.

The main bottleneck was video, not the CPU.

Had we immediately rewritten the Z80, even an excellent speed-up would have had limited effect on the overall frame time.

## The pixels were being processed three times

A more detailed profile exposed an inefficient chain:

1. the VDP renderer wrote palette indices into an internal scanline buffer;
2. a separate pass converted those indices into 16-bit colours in a second buffer;
3. another pass converted the colours to RGB444 and copied them into screen memory.

Each frame contained roughly 49,000 game pixels. A large share of them was being read and written multiple times.

It looked like an obvious opportunity to remove work. Before attempting that, however, there was a cheaper test.

## Optimisation 1: raise the compiler optimisation level

The core turned out to be built with only moderate optimisation. The ARM compiler template was changed to the highest level available in the toolchain.

The result:

| Version | Speed | Change |
|---|---:|---:|
| Baseline | 12.5 FPS | — |
| Maximum compiler optimisation | 13.5 FPS | about +8% |

Not a miracle, but free performance without changing program logic.

## Optimisation 2: combine colour conversion and output

The SMS Plus palette was converted directly into the display’s RGB444 format. The pass that had previously written an intermediate 16-bit buffer now wrote the final pixel straight to the correct framebuffer location.

The separate `BlitFrame()` pass disappeared.

The expectation was extremely optimistic: two passes had become one, so the improvement should have been dramatic.

Reality:

| Version | Speed | Change from previous |
|---|---:|---:|
| After compiler optimisation | 13.5 FPS | — |
| Remap directly into framebuffer | 14.9 FPS | about +10% |

The work had not vanished completely; part of it had moved. The old intermediate pass wrote to ordinary RAM. The new pass wrote directly into slower screen memory while accounting for the wide stride and centred image.

There were fewer passes, but the framebuffer was an expensive destination.

## Optimisation 3: stop waking the window server every frame

The direct-output proof of concept called `Invalidate()` and `Flush()` after every frame. They had helped produce a stable first version, but after debugging they became suspects for unnecessary overhead.

The new arrangement serviced window-server events less frequently, and direct output no longer triggered the complete call sequence on every frame.

The result:

| Version | Speed | Change from previous |
|---|---:|---:|
| Remap directly into framebuffer | 14.9 FPS | — |
| No per-frame `Invalidate()`/`Flush()` | 15.2 FPS | about +2% |

The hypothesis barely paid off. The apparently large overhead was mostly measurement noise and differences between test scenes.

That was still a useful result: a negative experiment closed a direction that could otherwise have consumed several more days.

## End result of the stage

Performance rose from **12.5 to 15.2 FPS**, an improvement of roughly 22%.

<!-- Add chart or screenshot of table: 12.5 → 13.5 → 14.9 → 15.2 FPS -->

The difference is visible on the device. The title screen is smoother, the picture remains stable and the colours are correct. It is not yet a complete game experience: input is still missing, and reaching 30 FPS requires almost another doubling of performance.

The current profile is approximately:

| Stage | Time per frame |
|---|---:|
| Background and sprite rendering | ~21 ms |
| Conversion and framebuffer write | ~19 ms |
| Z80 | ~18 ms |
| Other work | a few ms |

The next candidates are clear:

1. accelerate background and sprite construction;
2. rewrite the index → RGB444 → framebuffer path for ARMv4T;
3. only then return to the Z80 core;
4. once performance is under control, add frame pacing and keyboard input.

## What disciplined measurement changed

During a single stage, the numbers contradicted intuition several times:

- the main bottleneck was rendering, not the Z80;
- removing an entire pass produced about ten percent, not the expected one-third;
- the supposedly expensive window-server calls accounted for only a few percent.

On weak hardware, it is especially easy to confuse an elegant idea with a useful optimisation. The only reliable distinction is to measure before and after the change, on the same device and in the same scene.

The next phase of the project is ARM assembly for the heaviest rendering paths — and, finally, keyboard input on the Nokia 9210i.

---

**Stage result:** 12.5 → 15.2 FPS (+22%) on original hardware.  
**Main bottleneck:** VDP/rendering, not the Z80.
