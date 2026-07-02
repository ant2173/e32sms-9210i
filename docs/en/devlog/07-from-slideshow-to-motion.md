# Sonic on the Nokia 9210i, Part 7: From Slideshow to Motion

**Development log — July 2, 2026**  
[Русская версия](../../ru/devlog/07-from-slideshow-to-motion.md)

In the previous part, profiling E32SMS revealed an inconvenient but useful fact: the main bottleneck was not Z80 emulation but rendering. This session produced the most visible improvement in the project so far. Sonic still does not run at the full speed of the original console, but the result no longer looks like a sequence of static slides.

The gain did not come from one magical optimization. It came from two very different techniques. The first made game logic run much faster by deliberately skipping some rendered frames. The second reduced the cost of writing pixels to video memory. Together they also forced a distinction that emulator discussions often blur: **game speed** and **display smoothness are not the same metric**.

## Starting point

After the previous round of optimizations, the emulator was running at roughly **15.2 logic frames and 15.2 rendered frames per second**. That was already a respectable result for a Nokia 9210i with a roughly 52 MHz ARM920T, but an NTSC game designed around 60 updates per second was still almost four times faster.

Profiling showed an approximate per-frame cost of:

- about 42 ms in VDP rendering;
- about 18 ms in Z80 emulation;
- within rendering, roughly 19 ms converting indexed pixels and writing them to VRAM.

The emulated CPU was not the main problem. The Nokia was spending most of its time producing and moving the picture.

## Speedup #1: using the frameskip already built into the core

A classic technique on underpowered hardware is **frameskip**: some video frames are not drawn, while the game itself continues to advance. The Z80 still executes instructions, timers continue to tick and the game state moves forward, but the expensive graphics path is not run every time.

SMS Plus already supported this. Its frame function accepts a `skip_render` flag, but E32SMS had always called it with rendering enabled. I changed the loop to `frameskip = 1`: draw one frame, then process the next one without rendering it.

This produced the largest single gain in the project so far:

| Step | Logic rate | Rendered rate |
|---|---:|---:|
| Before frameskip | 14.9 fps | 14.9 fps |
| Frameskip = 1 | 23.5 fps | 11.7 fps |

Game logic became about **54% faster**. Sonic's attract mode immediately looked much more alive and stopped resembling a PowerPoint presentation.

The numbers need to be stated honestly, however. This is not “23.5 fps of smooth video.” The emulator advances the game 23.5 times per second, but updates the display only about 11.7 times per second. I therefore added two separate counters to the log:

- **logic fps** — complete game updates processed per second;
- **rendered fps** — frames actually written to the screen.

Frameskip improves the pace of the game, not the smoothness of the display. On the small 9210i screen the result looks better than a bare figure of 11–12 fps might suggest, but skipped frames still exist and are visible when you look for them. It is a trade-off, not free performance.

## Speedup #2: writing two pixels at a time

The next target was the path that writes finished RGB444 pixels into the Nokia's physical framebuffer. Until now, the code stored each 16-bit pixel separately.

The 9210i framebuffer geometry makes paired writes safe:

- pixel memory begins at an aligned `+32` byte offset after the palette table;
- the stride is 1280 bytes;
- the emulated image is 256 pixels wide;
- the centered destination is also four-byte aligned.

That means two adjacent 16-bit RGB444 pixels can be packed into one 32-bit word and written with a single store. The number of accesses to slow video memory is cut in half.

The measured result for this stage was:

- before: about **19.8 ms** per rendered frame;
- after: about **14.8 ms**;
- improvement: roughly **25%** in the VRAM write path.

The overall gain was smaller because framebuffer output is only one part of the workload. Total performance rose from 23.5 to about **24.5 logic fps**, while the displayed rate moved from 11.7 to **12.2 rendered fps**.

## The render pipeline after the change

With the new profiling counters, one actually rendered Sonic title-screen frame breaks down approximately as follows:

| Stage | Time per rendered frame |
|---|---:|
| Background (`render_bg`) | ~11.5 ms |
| Pixel conversion and VRAM write | ~14.8 ms |
| Sprites (`render_obj`) | ~8.8 ms |
| Tile-cache update | ~2.6 ms |
| Other rendering overhead | ~11 ms |
| **Total render time** | **~49 ms** |

The background renderer is already reasonably efficient: it moves four pixels at a time and caches expensive lookups. VRAM output now uses paired stores. Sprite rendering is less elegant — it scans all 64 sprite entries on every line — so there is still a possible C-level optimization in building visible-sprite lists ahead of time. But that stage is smaller than the two main costs.

## Where the cheap wins end

This is the new boundary of the project. The obvious architectural waste has been removed, redundant buffer passes are gone and framebuffer stores are wider. Large additional gains in the two remaining hot loops are becoming difficult to obtain in ordinary C.

The next major performance step will probably require ARM assembly:

- a dedicated blitter that converts indexed pixels to RGB444 and writes them to VRAM;
- possibly assembly versions of the hottest background-rendering loops.

That is no longer a small evening patch. It means ARMv4T code, gcc 2.9-psion and hardware where a mistake is more likely to produce a freeze or `KERN-EXEC` than a useful stack trace. It deserves its own development stage, controlled tests and a known-good build kept nearby.

One worthwhile C-level idea remains: avoid checking all 64 sprites on each of 192 scanlines. Even a perfect result there, however, will not double total performance.

## What the result actually means

Measured from the original **12.5 fps** at the beginning of this profiling arc, game-logic speed has almost doubled to **24.5 fps**. That is about **41% of the 60 Hz update rate** expected by the NTSC game.

The claim needs careful wording:

- this is not full speed;
- the screen itself updates roughly 12 times per second;
- there is no input yet, so the build cannot honestly be called playable;
- but Sonic is now visibly running rather than jumping between occasional static poses.

For software running on a 2001 communicator, this is an important transition: from a proof that the emulator can boot to a game scene that can be watched without pain.

## The next choice: assembly or input

From a pure optimization perspective, the next challenge is obvious: an ARM blitter. From a user's perspective, however, something else now offers more value: **controls**.

At the moment the Nokia can only play Sonic's attract mode by itself. Mapping the Communicator keyboard to the Master System controller would finally let us measure not only graphics and timing but actual responsiveness. At 24–25 logic updates per second, that test is now meaningful.

So the immediate goal is to make Sonic respond to the Nokia keyboard. Assembly can wait a little longer. The device now needs to stop being a display case and become a game machine.

To be continued.

---

**Current result:** ~24.5 logic fps / ~12.2 rendered fps, `frameskip = 1`.  
**Hardware:** Nokia 9210i, Symbian OS 6.0, EKA1, ARM920T ~52 MHz, 640×200 Color4K display.  
**Build:** Symbian SDK9210, gcc 2.9-psion, ARMI UREL.  
**Core:** SMS Plus by Charles MacDonald.  
**Development method:** code and documentation are produced with AI assistance; builds, measurements and conclusions are validated on an original Nokia 9210i.
